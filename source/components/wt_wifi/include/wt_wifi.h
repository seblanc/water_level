/*
 * Shared Wi-Fi station (run #12).
 *
 * One owner for esp_netif / esp_wifi, used by every network feature (ha_mqtt,
 * web_report). Credentials: NVS namespace "wt_cfg" keys wifi_ssid / wifi_pass,
 * falling back to CONFIG_WT_WIFI_SSID / CONFIG_WT_WIFI_PASSWORD.
 * Reconnects on its own with a 1 -> 60 s backoff.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start the station. Call from app_main only (not thread-safe). Idempotent:
 * later calls return the result of the first one.
 * Returns ESP_OK when started, ESP_ERR_NOT_FOUND when no SSID is configured
 * (nothing is initialised), or the failing IDF error.
 * Register IP_EVENT_STA_GOT_IP handlers before the first call, or check
 * wt_wifi_is_connected() afterwards.
 */
esp_err_t wt_wifi_start(void);

/* True while the station has an IPv4 address. */
bool wt_wifi_is_connected(void);

/* Block up to timeout_ms for an IPv4 address. False on timeout or if never started. */
bool wt_wifi_wait_connected(uint32_t timeout_ms);

/* RSSI of the current AP in dBm, or 0 when not connected. */
int wt_wifi_rssi(void);

#ifdef __cplusplus
}
#endif
