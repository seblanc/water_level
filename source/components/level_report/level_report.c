#include "level_report.h"

#include <stdbool.h>
#include <stddef.h>

#include "level_report_priv.h"

typedef struct {
    level_sink_fn fn;
    void         *ctx;
} sink_slot_t;

/* Written only during start-up (see level_report_add_sink), then read-only. */
static sink_slot_t s_sinks[LEVEL_REPORT_MAX_SINKS];
static size_t s_sink_count;
static bool s_initialised;

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

esp_err_t level_report_add_sink(level_sink_fn fn, void *ctx)
{
    if (fn == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_sink_count >= LEVEL_REPORT_MAX_SINKS) {
        return ESP_ERR_NO_MEM;
    }
    s_sinks[s_sink_count].fn = fn;
    s_sinks[s_sink_count].ctx = ctx;
    s_sink_count++;
    return ESP_OK;
}

esp_err_t level_report_init(void)
{
    if (s_initialised) {
        return ESP_ERR_INVALID_STATE;
    }
    /* The console UART itself is set up by ESP-IDF. */
    esp_err_t err = level_report_add_sink(level_report_console_sink, NULL);
    if (err == ESP_OK) {
        s_initialised = true;
    }
    return err;
}

void level_report_publish(const level_reading_t *r)
{
    if (r == NULL) {
        return;
    }
    for (size_t i = 0; i < s_sink_count; i++) {
        s_sinks[i].fn(r, s_sinks[i].ctx);
    }
}
