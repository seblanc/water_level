/*
 * Home Assistant integration over MQTT (Stage 2, SYSTEM_ARCHITECTURE.md §5).
 *
 * Registers a level_report sink that hands readings to a background
 * publisher task. Wi-Fi is owned by wt_wifi; the MQTT client starts once the
 * station has an IP. Network failures are retried and never stop the console
 * report stream.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load the settings (NVS namespace "wt_cfg", Kconfig fallback per key) and,
 * if a Wi-Fi SSID and a broker URI are configured, create the MQTT client
 * and the publisher task. Wi-Fi itself is started by wt_wifi_start().
 *
 * Call once from app_main, after nvs_flash_init() and level_report_init(),
 * and before the reporter task is created (level_report_add_sink rule).
 *
 * Returns ESP_OK when HA is running or deliberately disabled (not
 * configured). Any other value means HA is off; the error has already been
 * logged and the caller should carry on without HA.
 */
esp_err_t ha_mqtt_start(void);

#ifdef __cplusplus
}
#endif
