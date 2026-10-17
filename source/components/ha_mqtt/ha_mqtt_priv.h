/* Component-internal: runtime settings shared by ha_config.c and ha_mqtt.c. */
#pragma once

#include "esp_err.h"

/* Sizes include the NUL; limits follow wifi_config_t (32/64) and §5.3. */
typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char mqtt_uri[129];
    char mqtt_user[65];
    char mqtt_pass[65];
} ha_config_t;

/*
 * Fill cfg from NVS namespace "wt_cfg", falling back to the Kconfig value for
 * every key that is missing or unreadable. A missing namespace is not an
 * error. Always returns ESP_OK (problems are logged as warnings).
 */
esp_err_t ha_config_load(ha_config_t *cfg);
