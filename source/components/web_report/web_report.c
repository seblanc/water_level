#include "web_report.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "level_report.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "web_payload.h"
#include "wt_wifi.h"

/* Below the reporter (4) and sensor RX (5) tasks, like wt_ha. TLS needs the large stack. */
#define WEB_TASK_PRIO        3
#define WEB_TASK_STACK       8192
#define WEB_HTTP_TIMEOUT_MS  10000
/* After a failed post, wait this long before the next try (not the full period). */
#define WEB_RETRY_MS         15000
#define WEB_NVS_NAMESPACE    "wt_cfg"
#define WEB_URL_MAX          160
#define WEB_KEY_MAX          65

static const char *TAG = "web";

static char s_url[WEB_URL_MAX];
static char s_key[WEB_KEY_MAX];
static char s_device[WEB_PAYLOAD_DEVICE_LEN];
static QueueHandle_t s_queue;
static bool s_started;

/* wt_web task only. */
static char s_form[WEB_PAYLOAD_FORM_MAX];

/* Start of the server reply body (post.php replies with ~60 bytes of JSON). */
typedef struct {
    char buf[192];
    int len;
} web_body_t;
static web_body_t s_body;

/* ---- settings (same NVS rules as ha_config.c / wt_wifi.c) ---- */

static void load_key(nvs_handle_t h, bool have_nvs, const char *key, char *dst, size_t size,
                     const char *fallback)
{
    if (have_nvs) {
        size_t len = size;
        esp_err_t err = nvs_get_str(h, key, dst, &len);
        if (err == ESP_OK) {
            return;
        }
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "nvs key %s unreadable (%s), using Kconfig value", key,
                     esp_err_to_name(err));
        }
    }
    strlcpy(dst, fallback, size);
}

static void load_settings(void)
{
    nvs_handle_t h = 0;
    bool have_nvs = nvs_open(WEB_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK;
    load_key(h, have_nvs, "web_url", s_url, sizeof(s_url), CONFIG_WT_WEB_POST_URL);
    load_key(h, have_nvs, "web_key", s_key, sizeof(s_key), CONFIG_WT_WEB_API_KEY);
    if (have_nvs) {
        nvs_close(h);
    }
}

/* ---- level_report sink (reporter task context) ---- */

static void web_sink(const level_reading_t *r, void *ctx)
{
    (void)ctx;
    /* Depth-1 queue: the newest reading replaces the old one, never blocks. */
    xQueueOverwrite(s_queue, r);
}

/* ---- post task ---- */

/* Keeps the start of the final response body; runs inside esp_http_client_perform(). */
static esp_err_t http_event(esp_http_client_event_t *evt)
{
    web_body_t *b = evt->user_data;
    if (evt->event_id == HTTP_EVENT_HEADERS_SENT) {
        b->len = 0;  /* new request (also after a redirect) */
        b->buf[0] = '\0';
    } else if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        int room = (int)sizeof(b->buf) - 1 - b->len;
        int n = evt->data_len < room ? evt->data_len : room;
        if (n > 0) {
            memcpy(b->buf + b->len, evt->data, n);
            b->len += n;
            b->buf[b->len] = '\0';
        }
    }
    return ESP_OK;
}

/*
 * One blocking POST. Returns the HTTP status, or -1 with *err set on a
 * transport error. The reply body is left in s_body.
 */
static int post_reading(const level_reading_t *r, esp_err_t *err)
{
    *err = ESP_FAIL;
    const web_post_t p = {
        .key = s_key,
        .device = s_device,
        .distance_mm = r->status == LEVEL_STATUS_NO_DATA ? -1 : r->distance_mm,
        .status = level_status_str(r->status),
        .age_ms = r->age_ms,
        .frames_ok = r->frames_ok,
        .checksum_err = r->checksum_err,
        .rssi = wt_wifi_rssi(),
        .uptime_s = (uint32_t)(esp_timer_get_time() / 1000000),
        .fw = esp_app_get_description()->version,
    };
    int len = web_payload_form(s_form, sizeof(s_form), &p);
    if (len < 0) {
        ESP_LOGE(TAG, "form body too large");
        return -1;
    }

    const esp_http_client_config_t cfg = {
        .url = s_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = WEB_HTTP_TIMEOUT_MS,
        /* Used for https:// only; plain http:// ignores it. */
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event,
        .user_data = &s_body,
    };
    s_body.len = 0;
    s_body.buf[0] = '\0';
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        *err = ESP_ERR_NO_MEM;
        return -1;
    }
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, s_form, len);

    int status = -1;
    *err = esp_http_client_perform(client);
    if (*err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    return status;
}

static void post_task(void *arg)
{
    (void)arg;
    const int64_t period_us = (int64_t)CONFIG_WT_WEB_POST_PERIOD_MS * 1000;
    const int64_t retry_us = (int64_t)WEB_RETRY_MS * 1000;
    int64_t last_try_us = 0;
    bool last_ok = false;
    bool posted = false;          /* at least one successful post since boot */
    bool posted_has_data = false; /* has_data of the last successful post */
    bool logged_fail = false;     /* the current outage already has its W line */
    bool stack_logged = false;
    level_reading_t r;

    for (;;) {
        if (xQueueReceive(s_queue, &r, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!wt_wifi_is_connected()) {
            continue;  /* no backlog while offline */
        }

        bool has_data = (r.status != LEVEL_STATUS_NO_DATA);
        int64_t now = esp_timer_get_time();
        if (last_try_us != 0 && !last_ok && now - last_try_us < retry_us) {
            continue;  /* back off after a failure, NO_DATA transitions included */
        }
        bool due = !posted || !last_ok || has_data != posted_has_data ||
                   now - last_try_us >= period_us;
        if (!due) {
            continue;
        }

        last_try_us = now;
        esp_err_t err;
        int status = post_reading(&r, &err);
        /* A 200 only counts when post.php itself answered {"ok":true}: a hosting
         * error page or an unexecuted PHP file can also come back as 200. */
        web_reply_t reply = {0};
        bool ok = (status == 200) && web_payload_parse_reply(s_body.buf, &reply) == 0;
        if (ok) {
            /* One I line per stored reading (at most one per period): the
             * server-side row id is the proof that the database got it.
             * No key=value tokens here, the bench checker samples those. */
            if (reply.id >= 0 && reply.rows >= 0) {
                ESP_LOGI(TAG, "post ok: http 200, stored as row %" PRId64 ", %" PRId64
                         " rows in table", reply.id, reply.rows);
            } else {
                ESP_LOGI(TAG, "post ok: http 200, stored (server sent no row id)");
            }
            posted = true;
            posted_has_data = has_data;
            logged_fail = false;
        } else if (!logged_fail) {
            /* One W line per outage start, not one per retry (V-16e). */
            if (status == 200) {
                ESP_LOGW(TAG, "post rejected: http 200 but the reply is not from post.php");
            } else if (status > 0) {
                /* 3xx = URL redirects, 401 = wrong API key, 404 = wrong URL,
                 * 5xx = server/database. */
                ESP_LOGW(TAG, "post rejected: http %d", status);
            } else {
                ESP_LOGW(TAG, "post failed: %s", esp_err_to_name(err));
            }
            logged_fail = true;
        }
        last_ok = ok;

        if (!stack_logged) {
            stack_logged = true;
            ESP_LOGI(TAG, "wt_web stack free=%u B", (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }
    }
}

/* ---- start-up ---- */

esp_err_t web_report_start(void)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_started = true;

    load_settings();
    if (s_url[0] == '\0') {
        ESP_LOGI(TAG, "disabled (no web_url configured)");
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;
    uint8_t mac[6];
    ESP_GOTO_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), fail, TAG, "read mac");
    web_payload_device_id(s_device, sizeof(s_device), mac);

    /* Never deleted: once the sink is registered the reporter task writes to it. */
    s_queue = xQueueCreate(1, sizeof(level_reading_t));
    ESP_GOTO_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, fail, TAG, "queue");
    ESP_GOTO_ON_ERROR(level_report_add_sink(web_sink, NULL), fail, TAG, "sink");

    /* The IDF HTTP/TLS stack logs every failed attempt at E level; post_reading()
     * already reports it as one "web: post failed: <err>" W line, and the bench
     * checker treats any E line as a firmware fault. */
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);
    esp_log_level_set("esp-tls", ESP_LOG_NONE);
    esp_log_level_set("esp-tls-mbedtls", ESP_LOG_NONE);
    esp_log_level_set("transport_base", ESP_LOG_NONE);
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);

    ESP_GOTO_ON_FALSE(xTaskCreate(post_task, "wt_web", WEB_TASK_STACK, NULL, WEB_TASK_PRIO,
                                  NULL) == pdPASS,
                      ESP_ERR_NO_MEM, fail, TAG, "post task");

    /* The API key is never logged. */
    ESP_LOGI(TAG, "enabled device=%s url=%s key=%s period_ms=%d", s_device, s_url,
             s_key[0] != '\0' ? "set" : "none", CONFIG_WT_WEB_POST_PERIOD_MS);
    return ESP_OK;

fail:
    /* Runs at most once; a registered sink with no task only fills its depth-1 queue. */
    ESP_LOGE(TAG, "init failed: %s; web report off", esp_err_to_name(ret));
    return ret;
}
