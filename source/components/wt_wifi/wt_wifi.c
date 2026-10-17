#include "wt_wifi.h"

#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "sdkconfig.h"

#define WIFI_NVS_NAMESPACE  "wt_cfg"
#define WIFI_RETRY_MIN_S    1
#define WIFI_RETRY_MAX_S    60
#define WIFI_BIT_CONNECTED  BIT0

static const char *TAG = "wt_wifi";

static bool s_started;
static esp_err_t s_start_result = ESP_ERR_INVALID_STATE;
static EventGroupHandle_t s_events;
static esp_timer_handle_t s_retry_timer;

/* Event loop / esp_timer task only. */
static uint32_t s_retry_delay_s = WIFI_RETRY_MIN_S;

/* ---- credentials (same keys and fallbacks as ha_config.c) ---- */

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

static void load_credentials(char *ssid, size_t ssid_size, char *pass, size_t pass_size)
{
    nvs_handle_t h = 0;
    bool have_nvs = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK;
    load_key(h, have_nvs, "wifi_ssid", ssid, ssid_size, CONFIG_WT_WIFI_SSID);
    load_key(h, have_nvs, "wifi_pass", pass, pass_size, CONFIG_WT_WIFI_PASSWORD);
    if (have_nvs) {
        nvs_close(h);
    }
}

/* ---- reconnect (event loop and esp_timer task context) ---- */

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
            ESP_LOGW(TAG, "reconnect timer: %s", esp_err_to_name(err));
        }
        return 0;
    }
    s_retry_delay_s = (delay_s >= WIFI_RETRY_MAX_S / 2) ? WIFI_RETRY_MAX_S : delay_s * 2;
    return delay_s;
}

static void try_connect(void)
{
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        uint32_t delay_s = schedule_reconnect();
        if (delay_s > 0) {
            ESP_LOGW(TAG, "connect failed (%s), retry in %" PRIu32 " s", esp_err_to_name(err),
                     delay_s);
        } else {
            ESP_LOGW(TAG, "connect failed (%s), retry pending", esp_err_to_name(err));
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
        xEventGroupClearBits(s_events, WIFI_BIT_CONNECTED);
        uint32_t delay_s = schedule_reconnect();
        if (delay_s > 0) {
            ESP_LOGW(TAG, "disconnected reason=%d, retry in %" PRIu32 " s", reason, delay_s);
        } else {
            ESP_LOGW(TAG, "disconnected reason=%d, retry pending", reason);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        s_retry_delay_s = WIFI_RETRY_MIN_S;
        ESP_LOGI(TAG, "connected ip=" IPSTR " rssi=%d", IP2STR(&ev->ip_info.ip), wt_wifi_rssi());
        xEventGroupSetBits(s_events, WIFI_BIT_CONNECTED);
    }
}

/* ---- start-up ---- */

static esp_err_t start(void)
{
    esp_err_t ret = ESP_OK;
    char ssid[33];
    char pass[65];

    load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
    if (ssid[0] == '\0') {
        ESP_LOGI(TAG, "disabled (no wifi_ssid configured)");
        return ESP_ERR_NOT_FOUND;
    }

    s_events = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_events != NULL, ESP_ERR_NO_MEM, TAG, "event group");

    const esp_timer_create_args_t timer_args = {
        .callback = retry_timer_cb,
        .name = "wifi_retry",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_retry_timer), TAG, "retry timer");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ret = esp_event_loop_create_default();
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = ESP_OK;  /* already created */
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "event loop");
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(sta != NULL, ESP_ERR_NO_MEM, TAG, "wifi netif");
    /* Sent as DHCP option 12 (router client list). Set before the first DHCP request. */
    esp_err_t host_err = esp_netif_set_hostname(sta, CONFIG_WT_WIFI_HOSTNAME);
    if (host_err != ESP_OK) {
        /* Not fatal: lwIP keeps CONFIG_LWIP_LOCAL_HOSTNAME. */
        ESP_LOGW(TAG, "hostname %s rejected (%s)", CONFIG_WT_WIFI_HOSTNAME,
                 esp_err_to_name(host_err));
    }

    esp_log_level_set("wifi", ESP_LOG_WARN);
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init), TAG, "wifi init");
    /* Credentials come from wt_cfg / Kconfig; do not let the driver copy them into NVS. */
    ESP_GOTO_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), fail, TAG, "wifi storage");

    ESP_GOTO_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                          on_net_event, NULL, NULL),
                      fail, TAG, "wifi handler");
    ESP_GOTO_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                          on_net_event, NULL, NULL),
                      fail, TAG, "ip handler");

    wifi_config_t wifi_cfg;
    memset(&wifi_cfg, 0, sizeof(wifi_cfg));
    memcpy(wifi_cfg.sta.ssid, ssid, strnlen(ssid, sizeof(wifi_cfg.sta.ssid)));
    memcpy(wifi_cfg.sta.password, pass, strnlen(pass, sizeof(wifi_cfg.sta.password)));
    wifi_cfg.sta.threshold.authmode = pass[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_GOTO_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), fail, TAG, "wifi mode");
    ESP_GOTO_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), fail, TAG,
                      "wifi config (password must be empty or >= 8 characters)");

    /* The password is never logged. */
    ESP_LOGI(TAG, "starting ssid=%s, hostname %s", ssid, CONFIG_WT_WIFI_HOSTNAME);
    /* Last step: STA_START triggers the first connect attempt. */
    ESP_GOTO_ON_ERROR(esp_wifi_start(), fail, TAG, "wifi start");
    return ESP_OK;

fail:
    /* Event handler instances and the STA netif stay registered (F-26): harmless, runs once. */
    esp_wifi_deinit();
    return ret;
}

esp_err_t wt_wifi_start(void)
{
    if (!s_started) {
        s_started = true;
        s_start_result = start();
        if (s_start_result != ESP_OK && s_start_result != ESP_ERR_NOT_FOUND) {
            /* W, not E: the console stream continues without the network. */
            ESP_LOGW(TAG, "init failed: %s; wifi off", esp_err_to_name(s_start_result));
        }
    }
    return s_start_result;
}

bool wt_wifi_is_connected(void)
{
    return s_events != NULL && (xEventGroupGetBits(s_events) & WIFI_BIT_CONNECTED) != 0;
}

bool wt_wifi_wait_connected(uint32_t timeout_ms)
{
    if (s_events == NULL || s_start_result != ESP_OK) {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(s_events, WIFI_BIT_CONNECTED, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_BIT_CONNECTED) != 0;
}

int wt_wifi_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return 0;
    }
    return ap.rssi;
}
