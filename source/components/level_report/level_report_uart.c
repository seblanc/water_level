#include <inttypes.h>

#include "esp_log.h"
#include "level_report_priv.h"

static const char *TAG = "wt";

void level_report_console_sink(const level_reading_t *r, void *ctx)
{
    (void)ctx;

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
