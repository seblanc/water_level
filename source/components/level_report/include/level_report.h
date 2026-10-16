/*
 * Level report sink.
 *
 * Stage 1: one key=value line per report on the console (UART0).
 * Stage 2: a Home Assistant sink (MQTT) is meant to be added behind this same
 * interface, without touching the sensor driver or the reporter task.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LEVEL_STATUS_OK = 0,
    LEVEL_STATUS_NO_DATA,       /* no fresh checksum-valid frame */
    LEVEL_STATUS_OUT_OF_RANGE,  /* fresh frame, but outside the sensor's nominal range */
} level_status_t;

typedef struct {
    level_status_t status;
    int32_t  distance_mm;     /* -1 when status is NO_DATA */
    int32_t  age_ms;          /* age of the newest frame; -1 if none ever received */
    uint32_t frames_ok;
    uint32_t checksum_err;
    uint32_t sync_skips;
    uint32_t uart_overflows;
    uint32_t bytes_rx;        /* raw bytes received from the sensor UART */
    uint32_t line_errors;     /* UART framing/parity errors */
} level_reading_t;

esp_err_t level_report_init(void);

void level_report_publish(const level_reading_t *r);

const char *level_status_str(level_status_t status);

#ifdef __cplusplus
}
#endif
