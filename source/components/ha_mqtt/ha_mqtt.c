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
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ha_mqtt_priv.h"
#include "ha_payload.h"
#include "level_report.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

/* Below the reporter (4) and sensor RX (5) tasks: network work never delays them. */
#define HA_TASK_PRIO          3
#define HA_TASK_STACK         4096
#define HA_RETRY_MIN_S        1
#define HA_RETRY_MAX_S        60
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
static esp_timer_handle_t s_retry_timer;
static bool s_started;

/* Event loop / esp_timer task only. */
static uint32_t s_retry_delay_s = HA_RETRY_MIN_S;
static bool s_mqtt_started;

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

/* ---- Wi-Fi reconnect (event loop and esp_timer task context) ---- */

/*
 * Arm the one-shot reconnect timer with the current delay, then double it (1 -> 60 s).
 * Returns the delay in s, or 0 if no new retry was armed (one already pending, or the
 * timer failed): the backoff only advances when the timer really starts (F-25).
 */
static uint32_t schedule_reconnect(void)
{
    uint32_t delay_s = s_retry_delay_s;
    esp_err_t err = esp_timer_start_once(s_retry_timer, (uint64_t)delay_s * 1000000ULL);
    if (err != ESP_OK) {
        if (err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "reconnect timer: %s", esp_err_to_name(err));
        }
        return 0;
    }
    s_retry_delay_s = (delay_s >= HA_RETRY_MAX_S / 2) ? HA_RETRY_MAX_S : delay_s * 2;
    return delay_s;
}

static void try_connect(void)
{
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        uint32_t delay_s = schedule_reconnect();
        if (delay_s > 0) {
            ESP_LOGW(TAG, "wifi connect failed (%s), retry in %" PRIu32 " s",
                     esp_err_to_name(err), delay_s);
        } else {
            ESP_LOGW(TAG, "wifi connect failed (%s), retry pending", esp_err_to_name(err));
        }
    }
}

static void retry_timer_cb(void *arg)
{
    (void)arg;
    try_connect();
}

static void on_net_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        try_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *ev = data;
        int reason = ev != NULL ? (int)ev->reason : -1;
        uint32_t delay_s = schedule_reconnect();
        if (delay_s > 0) {
            ESP_LOGW(TAG, "wifi disconnected reason=%d, retry in %" PRIu32 " s", reason,
                     delay_s);
        } else {
            ESP_LOGW(TAG, "wifi disconnected reason=%d, retry pending", reason);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        s_retry_delay_s = HA_RETRY_MIN_S;
        ESP_LOGI(TAG, "wifi connected ip=" IPSTR, IP2STR(&ev->ip_info.ip));
        if (!s_mqtt_started) {
            /* After the first start, esp-mqtt reconnects on its own. */
            esp_err_t err = esp_mqtt_client_start(s_client);
            if (err == ESP_OK) {
                s_mqtt_started = true;
            } else {
                ESP_LOGE(TAG, "mqtt start failed: %s", esp_err_to_name(err));
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
    bool wifi_inited = false;

    /* Never deleted: once the sink is registered the reporter task writes to it. */
    s_queue = xQueueCreate(1, sizeof(level_reading_t));
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue");

    const esp_timer_create_args_t timer_args = {
        .callback = retry_timer_cb,
        .name = "ha_retry",
    };
    ESP_GOTO_ON_ERROR(esp_timer_create(&timer_args, &s_retry_timer), fail, TAG, "retry timer");

    ESP_GOTO_ON_ERROR(esp_netif_init(), fail, TAG, "netif init");
    ret = esp_event_loop_create_default();
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = ESP_OK;  /* already created */
    }
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "event loop");
    ESP_GOTO_ON_FALSE(esp_netif_create_default_wifi_sta() != NULL, ESP_ERR_NO_MEM, fail, TAG,
                      "wifi netif");

    esp_log_level_set("wifi", ESP_LOG_WARN);
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_GOTO_ON_ERROR(esp_wifi_init(&wifi_init), fail, TAG, "wifi init");
    wifi_inited = true;
    /* Credentials come from wt_cfg; do not let the driver copy them into NVS. */
    ESP_GOTO_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), fail, TAG, "wifi storage");

    ESP_GOTO_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                          on_net_event, NULL, NULL),
                      fail, TAG, "wifi handler");
    ESP_GOTO_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                          on_net_event, NULL, NULL),
                      fail, TAG, "ip handler");

    wifi_config_t wifi_cfg;
    memset(&wifi_cfg, 0, sizeof(wifi_cfg));
    memcpy(wifi_cfg.sta.ssid, s_cfg.wifi_ssid,
           strnlen(s_cfg.wifi_ssid, sizeof(wifi_cfg.sta.ssid)));
    memcpy(wifi_cfg.sta.password, s_cfg.wifi_pass,
           strnlen(s_cfg.wifi_pass, sizeof(wifi_cfg.sta.password)));
    wifi_cfg.sta.threshold.authmode =
        s_cfg.wifi_pass[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_GOTO_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), fail, TAG, "wifi mode");
    ESP_GOTO_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), fail, TAG,
                      "wifi config (password must be empty or >= 8 characters)");

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

    /* Last step: STA_START triggers the first connect attempt. */
    ESP_GOTO_ON_ERROR(esp_wifi_start(), fail, TAG, "wifi start");
    return ESP_OK;

fail:
    /*
     * Nothing has been started at this point, so nothing can publish. The
     * queue (and the publisher task, if created) stay allocated but idle: a
     * registered sink cannot be removed. The Wi-Fi/IP event handler instances
     * and the default STA netif also stay registered (F-26): after
     * esp_wifi_deinit() no Wi-Fi events fire, and this path runs at most once.
     */
    if (s_client != NULL) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    if (wifi_inited) {
        esp_wifi_deinit();
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
        /* Runtime gate (§5.2): no netif, no Wi-Fi, no task. Stage 1 behaviour. */
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
