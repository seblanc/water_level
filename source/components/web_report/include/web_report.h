/*
 * Web report: POST readings to a PHP endpoint (run #14, server in web/water/).
 *
 * Registers a level_report sink that hands readings to a background task.
 * Once wt_wifi has an IP, the task POSTs the newest reading every
 * CONFIG_WT_WEB_POST_PERIOD_MS (and at once on NO_DATA transitions).
 * Network failures are retried and never stop the console report stream.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load the settings (NVS namespace "wt_cfg" keys web_url / web_key, Kconfig
 * fallback per key) and, if a URL is configured, start the post task.
 *
 * Call once from app_main, after nvs_flash_init() and level_report_init(),
 * and before the reporter task is created (level_report_add_sink rule).
 * Wi-Fi is started separately with wt_wifi_start().
 *
 * Returns ESP_OK when running or deliberately disabled (no URL). Any other
 * value means the web report is off; the error has already been logged.
 */
esp_err_t web_report_start(void);

#ifdef __cplusplus
}
#endif
