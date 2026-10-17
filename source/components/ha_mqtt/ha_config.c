#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "ha_mqtt_priv.h"
#include "nvs.h"
#include "sdkconfig.h"

#define HA_NVS_NAMESPACE "wt_cfg"

static const char *TAG = "ha";

/* Key present in NVS wins; otherwise use the Kconfig fallback (§5.3). */
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

esp_err_t ha_config_load(ha_config_t *cfg)
{
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(HA_NVS_NAMESPACE, NVS_READONLY, &h);
    bool have_nvs = (err == ESP_OK);
    if (!have_nvs && err != ESP_ERR_NVS_NOT_FOUND) {
        /* ESP_ERR_NVS_NOT_FOUND = namespace never written: normal for CI images. */
        ESP_LOGW(TAG, "nvs namespace %s not readable (%s), using Kconfig values",
                 HA_NVS_NAMESPACE, esp_err_to_name(err));
    }

    load_key(h, have_nvs, "wifi_ssid", cfg->wifi_ssid, sizeof(cfg->wifi_ssid),
             CONFIG_WT_WIFI_SSID);
    load_key(h, have_nvs, "wifi_pass", cfg->wifi_pass, sizeof(cfg->wifi_pass),
             CONFIG_WT_WIFI_PASSWORD);
    load_key(h, have_nvs, "mqtt_uri", cfg->mqtt_uri, sizeof(cfg->mqtt_uri),
             CONFIG_WT_MQTT_BROKER_URI);
    load_key(h, have_nvs, "mqtt_user", cfg->mqtt_user, sizeof(cfg->mqtt_user),
             CONFIG_WT_MQTT_USERNAME);
    load_key(h, have_nvs, "mqtt_pass", cfg->mqtt_pass, sizeof(cfg->mqtt_pass),
             CONFIG_WT_MQTT_PASSWORD);

    if (have_nvs) {
        nvs_close(h);
    }
    return ESP_OK;
}
