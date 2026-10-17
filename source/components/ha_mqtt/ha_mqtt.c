#include "ha_mqtt.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ha_mqtt_priv.h"
#include "ha_payload.h"
#include "level_report.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "wt_wifi.h"

/* Below the reporter (4) and sensor RX (5) tasks: network work never delays them. */
#define HA_TASK_PRIO          3
#define HA_TASK_STACK         4096
#define HA_MQTT_KEEPALIVE_S   30
#define HA_MQTT_RECONNECT_MS  10000
#define HA_ENTITY_COUNT       2

static const char *TAG = "ha";

static const ha_entity_t ENTITIES[HA_ENTITY_COUNT] = {HA_ENTITY_DISTANCE, HA_ENTITY_STATUS};

static ha_config_t s_cfg;
static char s_id[HA_PAYLOAD_ID_LEN];
static char s_topic_avail[HA_TOPIC_MAX];
static char s_topic_sensor[HA_TOPIC_MAX];
static char s_topic_state[HA_TOPIC_MAX];
/* Built once in ha_mqtt_start(), republished on every MQTT connect. */
static char s_disc_topic[HA_ENTITY_COUNT][HA_TOPIC_MAX];
static char s_disc_payload[HA_ENTITY_COUNT][HA_DISCOVERY_MAX];

static QueueHandle_t s_queue;
static esp_mqtt_client_handle_t s_client;
static bool s_started;

/* wt_ha task only. */
static bool s_mqtt_started;
static bool s_mqtt_start_error_logged;

/* Set by the MQTT event handler, read by the wt_ha task. */
static volatile bool s_mqtt_connected;
static volatile bool s_announce_pending;
static bool s_mqtt_error_logged;

/* ---- level_report sink (reporter task context) ---- */

static void ha_sink(const level_reading_t *r, void *ctx)
{
    (void)ctx;
    /* Depth-1 queue: the newest reading replaces the old one, never blocks. */
    xQueueOverwrite(s_queue, r);
}

/* ---- publisher task ---- */

static bool publish(const char *topic, const char *data, int qos, bool retain)
{
    /* len 0: esp-mqtt uses strlen(data). Returns a message id, or < 0 on failure. */
    return esp_mqtt_client_publish(s_client, topic, data, 0, qos, retain ? 1 : 0) >= 0;
}

static bool publish_announce(void)
{
    for (size_t i = 0; i < HA_ENTITY_COUNT; i++) {
        if (!publish(s_disc_topic[i], s_disc_payload[i], 1, true)) {
            return false;
        }
    }
    return publish(s_topic_avail, HA_AVAIL_ONLINE, 1, true);
}

/*
 * Wi-Fi is owned by wt_wifi (run #12). The MQTT client is started from this
 * task, once, as soon as the station has an IP; after that esp-mqtt
 * reconnects on its own. Polling here (once per reading) avoids a start race
 * between an IP event handler and ha_mqtt_start().
 */
static void start_mqtt_when_online(void)
{
    if (s_mqtt_started || !wt_wifi_is_connected()) {
        return;
    }
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err == ESP_OK) {
        s_mqtt_started = true;
    } else if (!s_mqtt_start_error_logged) {
        s_mqtt_start_error_logged = true;  /* retried silently with every reading */
        ESP_LOGE(TAG, "mqtt start failed: %s", esp_err_to_name(err));
    }
}

static void publisher_task(void *arg)
{
    (void)arg;
    const int64_t period_us = (int64_t)CONFIG_WT_HA_PUBLISH_PERIOD_MS * 1000;
    int64_t last_state_us = 0;
    bool state_sent = false;      /* a state was published since the last connect */
    bool sensor_sent = false;     /* the sensor topic holds sensor_has_data */
    bool sensor_has_data = false;
    char state[HA_STATE_MAX];
    level_reading_t r;

    for (;;) {
        if (xQueueReceive(s_queue, &r, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        start_mqtt_when_online();
        if (!s_mqtt_connected) {
            /* No backlog while offline: everything is re-sent after the next connect. */
            state_sent = false;
            sensor_sent = false;
            continue;
        }

        if (s_announce_pending) {
            s_announce_pending = false;
            if (!publish_announce()) {
                s_announce_pending = true;  /* retry with the next reading */
                continue;
            }
            ESP_LOGI(TAG, "discovery published, wt_ha stack free=%u B",
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
            state_sent = false;
            sensor_sent = false;
        }

        bool has_data = (r.status != LEVEL_STATUS_NO_DATA);
        bool changed = !sensor_sent || has_data != sensor_has_data;
        if (changed) {
            /* Sensor availability first, so HA never shows a NO_DATA distance. */
            if (!publish(s_topic_sensor, ha_payload_sensor_availability(has_data), 1, true)) {
                continue;
            }
            sensor_has_data = has_data;
            sensor_sent = true;
        }

        int64_t now = esp_timer_get_time();
        if (changed || !state_sent || now - last_state_us >= period_us) {
            const ha_state_t s = {
                .distance_mm = r.distance_mm,
                .status = level_status_str(r.status),
                .age_ms = r.age_ms,
                .frames_ok = r.frames_ok,
                .checksum_err = r.checksum_err,
            };
            if (ha_payload_state(state, sizeof(state), &s) >= 0 &&
                publish(s_topic_state, state, 0, false)) {
                last_state_us = now;
                state_sent = true;
            }
        }
    }
}

/* ---- MQTT events (esp-mqtt task context) ---- */

static void on_mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    const esp_mqtt_event_t *ev = data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "mqtt connected");
        s_mqtt_error_logged = false;
        s_announce_pending = true;
        s_mqtt_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        /* Also fires on every failed reconnect attempt: log the transition only. */
        if (s_mqtt_connected) {
            ESP_LOGW(TAG, "mqtt disconnected");
        }
        s_mqtt_connected = false;
        break;
    case MQTT_EVENT_ERROR:
        if (!s_mqtt_error_logged && ev != NULL && ev->error_handle != NULL) {
            s_mqtt_error_logged = true;  /* once per outage */
            if (ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGW(TAG, "mqtt connection refused (code %d), check user/password",
                         (int)ev->error_handle->connect_return_code);
            } else {
                ESP_LOGW(TAG, "mqtt connection error (type %d), retrying",
                         (int)ev->error_handle->error_type);
            }
        }
        break;
    default:
        break;
    }
}

/* ---- start-up ---- */

static esp_err_t build_names(void)
{
    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG, "read mac");
    const char *ver = esp_app_get_description()->version;

    bool ok = ha_payload_device_id(s_id, sizeof(s_id), mac) >= 0 &&
              ha_payload_topic(s_topic_avail, sizeof(s_topic_avail), s_id,
                               HA_TOPIC_AVAILABILITY) >= 0 &&
              ha_payload_topic(s_topic_sensor, sizeof(s_topic_sensor), s_id, HA_TOPIC_SENSOR) >= 0 &&
              ha_payload_topic(s_topic_state, sizeof(s_topic_state), s_id, HA_TOPIC_STATE) >= 0;
    for (size_t i = 0; ok && i < HA_ENTITY_COUNT; i++) {
        ok = ha_payload_discovery_topic(s_disc_topic[i], sizeof(s_disc_topic[i]),
                                        CONFIG_WT_HA_DISCOVERY_PREFIX, s_id, ENTITIES[i]) >= 0 &&
             ha_payload_discovery(s_disc_payload[i], sizeof(s_disc_payload[i]), s_id,
                                  ENTITIES[i], ver) >= 0;
    }
    ESP_RETURN_ON_FALSE(ok, ESP_ERR_INVALID_SIZE, TAG,
                        "topic/discovery build failed (prefix \"%s\", version \"%s\")",
                        CONFIG_WT_HA_DISCOVERY_PREFIX, ver);
    return ESP_OK;
}

static void log_enabled(void)
{
    char host[64];
    if (ha_payload_uri_host(host, sizeof(host), s_cfg.mqtt_uri) < 0) {
        strlcpy(host, "?", sizeof(host));
    }
    /* Passwords are never logged (§5.3). */
    ESP_LOGI(TAG, "enabled id=%s ssid=%s broker=%s user=%s period_ms=%d", s_id, s_cfg.wifi_ssid,
             host, s_cfg.mqtt_user[0] != '\0' ? s_cfg.mqtt_user : "-",
             CONFIG_WT_HA_PUBLISH_PERIOD_MS);
}

static esp_err_t start_network(void)
{
    esp_err_t ret = ESP_OK;

    /* Never deleted: once the sink is registered the reporter task writes to it. */
    s_queue = xQueueCreate(1, sizeof(level_reading_t));
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue");

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_cfg.mqtt_uri,
        .credentials = {
            .username = s_cfg.mqtt_user[0] != '\0' ? s_cfg.mqtt_user : NULL,
            .client_id = s_id,
            .authentication.password = s_cfg.mqtt_pass[0] != '\0' ? s_cfg.mqtt_pass : NULL,
        },
        .session = {
            .keepalive = HA_MQTT_KEEPALIVE_S,
            .last_will = {
                .topic = s_topic_avail,
                .msg = HA_AVAIL_OFFLINE,
                .qos = 1,
                .retain = 1,
            },
        },
        .network.reconnect_timeout_ms = HA_MQTT_RECONNECT_MS,
    };
    s_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_GOTO_ON_FALSE(s_client != NULL, ESP_ERR_INVALID_ARG, fail, TAG,
                      "mqtt client init (check the broker URI)");
    ESP_GOTO_ON_ERROR(esp_mqtt_client_register_event(s_client,
                                                     (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                                     on_mqtt_event, NULL),
                      fail, TAG, "mqtt handler");

    ESP_GOTO_ON_ERROR(level_report_add_sink(ha_sink, NULL), fail, TAG, "sink");
    ESP_GOTO_ON_FALSE(xTaskCreate(publisher_task, "wt_ha", HA_TASK_STACK, NULL, HA_TASK_PRIO,
                                  NULL) == pdPASS,
                      ESP_ERR_NO_MEM, fail, TAG, "publisher task");
    return ESP_OK;

fail:
    /*
     * The task was not created, so nothing can publish. The queue stays
     * allocated but idle (a registered sink cannot be removed); this path
     * runs at most once.
     */
    if (s_client != NULL) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    return ret;
}

esp_err_t ha_mqtt_start(void)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_started = true;

    ha_config_load(&s_cfg);
    if (s_cfg.wifi_ssid[0] == '\0' || s_cfg.mqtt_uri[0] == '\0') {
        /* Runtime gate (§5.2): no MQTT client, no task. */
        ESP_LOGI(TAG, "disabled (no wifi_ssid/mqtt_uri configured)");
        return ESP_OK;
    }

    esp_err_t err = build_names();
    if (err == ESP_OK) {
        log_enabled();
        err = start_network();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init failed: %s; HA off", esp_err_to_name(err));
    }
    return err;
}
