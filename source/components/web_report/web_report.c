#include "web_report.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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
/* Body excerpt in the "reply not from post.php" W line, NUL included. */
#define WEB_EXCERPT_LEN      96
/* Once the page showed a post, check it again after this many stored posts. */
#define WEB_PAGE_RECHECK     10

static const char *TAG = "web";

static char s_url[WEB_URL_MAX];
static char s_key[WEB_KEY_MAX];
static char s_device[WEB_PAYLOAD_DEVICE_LEN];
/* Web page address (directory of s_url) and its api.php, "" = no page check. */
static char s_page_dir[WEB_URL_MAX];
static char s_page_url[WEB_URL_MAX + sizeof(WEB_PAGE_API_QUERY)];
static QueueHandle_t s_queue;
static bool s_started;

/* wt_web task only. */
static char s_form[WEB_PAYLOAD_FORM_MAX];

/* Start of the server reply body. post.php replies with ~130 bytes of JSON;
 * api.php?hours=1 with ~1.5 KB, "count" after the history array. */
typedef struct {
    char buf[2048];
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

static web_post_t make_post(const level_reading_t *r)
{
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
    return p;
}

/*
 * One blocking request: a POST of form (len bytes) when form is non-NULL,
 * else a GET. Returns the HTTP status, or -1 with *err set on a transport
 * error. The reply body is left in s_body.
 */
static int http_request(const char *url, const char *form, int len, esp_err_t *err)
{
    const esp_http_client_config_t cfg = {
        .url = url,
        .method = form != NULL ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = WEB_HTTP_TIMEOUT_MS,
        /* Used for https:// only; plain http:// ignores it. */
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event,
        .user_data = &s_body,
    };
    *err = ESP_FAIL;
    s_body.len = 0;
    s_body.buf[0] = '\0';
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        *err = ESP_ERR_NO_MEM;
        return -1;
    }
    if (form != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
        esp_http_client_set_post_field(client, form, len);
    }

    int status = -1;
    *err = esp_http_client_perform(client);
    if (*err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    return status;
}

static int post_reading(const web_post_t *p, esp_err_t *err)
{
    int len = web_payload_form(s_form, sizeof(s_form), p);
    if (len < 0) {
        *err = ESP_FAIL;
        ESP_LOGE(TAG, "form body too large");
        return -1;
    }
    return http_request(s_url, s_form, len, err);
}

/* Log-safe start of a reply body that is not the expected JSON. */
static void body_excerpt(char *dst, size_t size)
{
    int n = web_payload_body_excerpt(dst, size, s_body.buf);
    if (n <= 0 && s_body.len > 0) {
        /* Only the start of the body is kept: a long <style> header can
         * leave no text in it although the body was not empty. */
        snprintf(dst, size, "no text in first %d bytes", s_body.len);
    } else if (n <= 0) {
        strlcpy(dst, "empty body", size);
    }
}

static void format_distance(char *dst, size_t size, int64_t mm)
{
    if (mm >= 0) {
        snprintf(dst, size, "%" PRId64 " mm", mm);
    } else {
        strlcpy(dst, "no distance", size);
    }
}

/*
 * GET api.php, the JSON the web page draws from, right after a stored post
 * and check that its latest reading is that post. Returns true if it is;
 * the W line for a failure is only written when log_failure is set.
 */
static bool check_page(const web_post_t *p, bool log_failure)
{
    esp_err_t err;
    int status = http_request(s_page_url, NULL, 0, &err);
    web_page_t page;
    web_payload_parse_page(s_body.buf, &page);
    web_page_check_t check = (status == 200) ? web_payload_check_page(&page, p)
                                             : WEB_PAGE_FAILED;
    if (check == WEB_PAGE_SHOWS_POST) {
        char count[48] = "";
        if (page.count >= 0) {
            snprintf(count, sizeof(count), ", %" PRId64 " readings in the last hour",
                     page.count);
        }
        ESP_LOGI(TAG, "page ok: %s shows this post as latest reading%s", s_page_dir, count);
        return true;
    }
    if (!log_failure) {
        return false;
    }

    if (check == WEB_PAGE_EMPTY) {
        /* post.php stored the row, so api.php reads another database or table. */
        ESP_LOGW(TAG, "page check: %s shows no data although the post was stored "
                 "(api.php and post.php use different config.php)", s_page_dir);
    } else if (check == WEB_PAGE_OTHER) {
        char shown[24];
        char sent[24];
        format_distance(shown, sizeof(shown), page.distance_mm);
        format_distance(sent, sizeof(sent), p->distance_mm >= 0 ? p->distance_mm : -1);
        ESP_LOGW(TAG, "page check: %s latest reading is not this post: device %s uptime %"
                 PRId64 " s (%s), sent device %s uptime %" PRIu32 " s (%s)", s_page_dir,
                 page.device[0] != '\0' ? page.device : "?", page.uptime_s, shown, p->device,
                 p->uptime_s, sent);
    } else if (status > 0 && page.error[0] != '\0') {
        ESP_LOGW(TAG, "page check failed: %sapi.php http %d (%s)", s_page_dir, status,
                 page.error);
    } else if (status > 0) {
        char excerpt[WEB_EXCERPT_LEN];
        body_excerpt(excerpt, sizeof(excerpt));
        ESP_LOGW(TAG, "page check failed: %sapi.php http %d, reply not from api.php (%s)",
                 s_page_dir, status, excerpt);
    } else {
        ESP_LOGW(TAG, "page check failed: %sapi.php %s", s_page_dir, esp_err_to_name(err));
    }
    return false;
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
    uint32_t failed_tries = 0;    /* failed posts since the last delivered one */
    bool page_ok = false;         /* the last page check found the post */
    bool page_logged_fail = false; /* the current page failure has its W line */
    uint32_t posts_since_page = 0;
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
        const web_post_t p = make_post(&r);
        esp_err_t err;
        int status = post_reading(&p, &err);
        /* A 200 only counts when post.php itself answered {"ok":true}: a hosting
         * error page or an unexecuted PHP file can also come back as 200. Since
         * run #20 post.php also reads the row back and echoes the stored values,
         * which must match what was sent. */
        web_reply_t reply;
        web_payload_parse_reply(s_body.buf, &reply);
        web_store_t store = (status == 200) ? web_payload_check_stored(&reply, &p)
                                            : WEB_STORE_FAILED;
        bool ok = (store == WEB_STORE_CONFIRMED || store == WEB_STORE_UNCONFIRMED);
        if (ok) {
            /* One I line per stored reading (at most one per period). No
             * key=value tokens here, the bench checker samples those. */
            char after[40] = "";
            if (failed_tries > 0) {
                snprintf(after, sizeof(after), ", after %" PRIu32 " failed tries", failed_tries);
            }
            if (store == WEB_STORE_CONFIRMED) {
                char dist[24];
                if (p.distance_mm >= 0) {
                    snprintf(dist, sizeof(dist), "%" PRId32 " mm", p.distance_mm);
                } else {
                    strlcpy(dist, "no distance", sizeof(dist));
                }
                ESP_LOGI(TAG, "post ok: http 200, row %" PRId64 " stored and read back (%s), %"
                         PRId64 " rows in table%s", reply.id, dist, reply.rows, after);
            } else if (reply.id >= 0) {
                ESP_LOGI(TAG, "post ok: http 200, stored as row %" PRId64 " (not read back, "
                         "old post.php)%s", reply.id, after);
            } else {
                ESP_LOGI(TAG, "post ok: http 200, stored (no row id, old post.php)%s", after);
            }
            posted = true;
            posted_has_data = has_data;
            logged_fail = false;
            failed_tries = 0;

            /* Does the web page show it? After every stored post until it
             * does, then every WEB_PAGE_RECHECK posts. */
            if (s_page_url[0] != '\0' && (!page_ok || ++posts_since_page >= WEB_PAGE_RECHECK)) {
                posts_since_page = 0;
                page_ok = check_page(&p, !page_logged_fail);
                page_logged_fail = !page_ok;
            }
        } else {
            failed_tries++;
            if (!logged_fail) {
                /* One W line per outage start, not one per retry (V-16e). */
                if (store == WEB_STORE_MISMATCH) {
                    ESP_LOGW(TAG, "post rejected: row %" PRId64 " read back with other values "
                             "(%" PRId64 " mm, uptime %" PRId64 " s)", reply.id,
                             reply.distance_mm, reply.uptime_s);
                } else if (status > 0 && reply.error[0] != '\0') {
                    /* 3xx = URL redirects, 401 = wrong API key, 404 = wrong URL,
                     * 5xx = server/database; post.php names the cause in "error". */
                    ESP_LOGW(TAG, "post rejected: http %d (%s)", status, reply.error);
                } else if (status > 0) {
                    /* Not post.php's JSON (hosting error page, PHP fatal error,
                     * unexecuted file): show the start of the body instead. */
                    char excerpt[WEB_EXCERPT_LEN];
                    body_excerpt(excerpt, sizeof(excerpt));
                    ESP_LOGW(TAG, "post rejected: http %d, reply not from post.php (%s)", status,
                             excerpt);
                } else {
                    ESP_LOGW(TAG, "post failed: %s", esp_err_to_name(err));
                }
                logged_fail = true;
            }
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
    if (web_payload_page_dir(s_page_dir, sizeof(s_page_dir), s_url) > 0) {
        strlcpy(s_page_url, s_page_dir, sizeof(s_page_url));
        strlcat(s_page_url, WEB_PAGE_API_QUERY, sizeof(s_page_url));
    }

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
    if (s_page_url[0] != '\0') {
        ESP_LOGI(TAG, "page check: %sapi.php after the first stored post, then every %d posts",
                 s_page_dir, WEB_PAGE_RECHECK);
    } else {
        ESP_LOGW(TAG, "page check off: no page address in the post url");
    }
    return ESP_OK;

fail:
    /* Runs at most once; a registered sink with no task only fills its depth-1 queue. */
    ESP_LOGE(TAG, "init failed: %s; web report off", esp_err_to_name(ret));
    return ret;
}
