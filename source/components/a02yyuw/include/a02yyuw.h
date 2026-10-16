/*
 * A02YYUW ultrasonic distance sensor driver (ESP-IDF).
 *
 * Installs a UART at 9600 8N1, runs an RX task that decodes frames, and keeps
 * the latest checksum-valid sample for other tasks to read.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define A02YYUW_BAUD_RATE 9600

typedef struct {
    uart_port_t uart_num;      /* UART_NUM_1 or UART_NUM_2 (UART0 is the console) */
    int         rx_gpio;       /* ESP32 RX <- sensor TX */
    int         tx_gpio;       /* ESP32 TX -> sensor RX; -1 = leave unconfigured */
    uint32_t    stale_ms;      /* sample older than this is not "fresh" */
    uint8_t     median_window; /* 1 = no filtering, up to A02YYUW_FILTER_MAX_WINDOW */
    UBaseType_t task_priority;
    uint32_t    task_stack;    /* bytes */
} a02yyuw_config_t;

typedef struct {
    uint16_t distance_mm;   /* filtered value (== raw_mm when the filter is off) */
    uint16_t raw_mm;        /* newest decoded frame */
    int64_t  timestamp_us;  /* esp_timer time of the newest frame */
    int32_t  age_ms;        /* time since the newest frame; -1 if none ever received */
    bool     in_range;      /* distance_mm within A02YYUW_MIN_MM..A02YYUW_MAX_MM */
} a02yyuw_sample_t;

typedef struct {
    uint32_t frames_ok;
    uint32_t checksum_err;
    uint32_t sync_skips;
    uint32_t uart_overflows;  /* FIFO or ring-buffer overflows (input was flushed) */
    uint32_t line_errors;     /* UART framing/parity errors */
    uint32_t bytes_rx;
} a02yyuw_stats_t;

/* Install the UART driver and start the RX task. Call once. */
esp_err_t a02yyuw_init(const a02yyuw_config_t *cfg);

/*
 * Copy the latest sample into *out. Returns true if a sample exists and is
 * not older than cfg.stale_ms. When false, *out still holds the last sample
 * (if any) so the caller can report its age.
 */
bool a02yyuw_get_latest(a02yyuw_sample_t *out);

void a02yyuw_get_stats(a02yyuw_stats_t *out);

#ifdef __cplusplus
}
#endif
