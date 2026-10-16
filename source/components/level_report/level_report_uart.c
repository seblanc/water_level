#include "level_report.h"

#include <inttypes.h>
#include <stddef.h>

#include "esp_log.h"

static const char *TAG = "wt";

const char *level_status_str(level_status_t status)
{
    switch (status) {
    case LEVEL_STATUS_OK:
        return "OK";
    case LEVEL_STATUS_NO_DATA:
        return "NO_DATA";
    case LEVEL_STATUS_OUT_OF_RANGE:
        return "OUT_OF_RANGE";
    default:
        return "UNKNOWN";
    }
}

esp_err_t level_report_init(void)
{
    /* The console UART is set up by ESP-IDF; nothing to do for Stage 1. */
    return ESP_OK;
}

void level_report_publish(const level_reading_t *r)
{
    if (r == NULL) {
        return;
    }

    /*
     * key=value format so a host script can parse the log during verification
     * (SYSTEM_ARCHITECTURE.md §8). New fields must only be appended.
     */
    ESP_LOGI(TAG, "distance_mm=%" PRId32 " status=%s age_ms=%" PRId32
             " ok=%" PRIu32 " cks_err=%" PRIu32 " sync_skip=%" PRIu32 " uart_ovf=%" PRIu32
             " rx_bytes=%" PRIu32 " line_err=%" PRIu32,
             r->distance_mm, level_status_str(r->status), r->age_ms,
             r->frames_ok, r->checksum_err, r->sync_skips, r->uart_overflows,
             r->bytes_rx, r->line_errors);
}
