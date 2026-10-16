/*
 * Level report dispatcher.
 *
 * level_report_publish() fans each reading out to every registered sink.
 * Stage 1: one sink, a key=value line on the console (UART0), registered by
 * level_report_init().
 * Stage 2: a Home Assistant sink (e.g. MQTT) registers itself with
 * level_report_add_sink(), without touching the sensor driver or the reporter
 * task.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Console sink + room for Stage 2 sinks. */
#define LEVEL_REPORT_MAX_SINKS 4

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

/*
 * A sink is called from the reporter task once per report period. It must
 * not block: a sink doing network I/O should copy the reading into its own
 * queue and return.
 */
typedef void (*level_sink_fn)(const level_reading_t *r, void *ctx);

/* Register the console sink. Call once, before the first publish. */
esp_err_t level_report_init(void);

/*
 * Register an additional sink. Not thread-safe: call during start-up, before
 * the reporter task starts publishing.
 * Returns ESP_ERR_INVALID_ARG for a NULL fn, ESP_ERR_NO_MEM when all
 * LEVEL_REPORT_MAX_SINKS slots are taken.
 */
esp_err_t level_report_add_sink(level_sink_fn fn, void *ctx);

/* Pass the reading to every registered sink, in registration order. */
void level_report_publish(const level_reading_t *r);

const char *level_status_str(level_status_t status);

#ifdef __cplusplus
}
#endif
