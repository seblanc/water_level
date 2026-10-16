/*
 * Water tank level firmware — Stage 1.
 *
 * Reads an A02YYUW ultrasonic sensor on UART2 (GPIO16/GPIO17 by default) and
 * prints the distance on the console once per report period (1 s default).
 */
#include <inttypes.h>
#include <stdlib.h>

#include "a02yyuw.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "level_report.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#ifdef CONFIG_WT_MEDIAN_FILTER
#define WT_MEDIAN_WINDOW CONFIG_WT_MEDIAN_WINDOW
#else
#define WT_MEDIAN_WINDOW 1
#endif

#define SENSOR_TASK_PRIO    5
#define SENSOR_TASK_STACK   3072
#define REPORT_TASK_PRIO    4
#define REPORT_TASK_STACK   4096

static const char *TAG = "wt";

static void init_nvs(void)
{
    /* Not used in Stage 1; prepared for Stage 2 (Wi-Fi/MQTT settings). */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void print_banner(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    uint32_t flash_size = 0;

    esp_chip_info(&chip);
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        flash_size = 0;
    }

    ESP_LOGI(TAG, "=== water_tank fw %s (built %s %s) ===", app->version, app->date, app->time);
    ESP_LOGI(TAG, "idf=%s chip_rev=v%d.%d cores=%d flash=%" PRIu32 "MB",
             esp_get_idf_version(), chip.revision / 100, chip.revision % 100, chip.cores,
             flash_size / (1024 * 1024));
    ESP_LOGI(TAG, "sensor=A02YYUW uart=%d rx_gpio=%d tx_gpio=%d baud=%d",
             CONFIG_WT_SENSOR_UART_NUM, CONFIG_WT_SENSOR_RX_GPIO, CONFIG_WT_SENSOR_TX_GPIO,
             A02YYUW_BAUD_RATE);
    ESP_LOGI(TAG, "report_period_ms=%d stale_ms=%d median_window=%d",
             CONFIG_WT_REPORT_PERIOD_MS, CONFIG_WT_SENSOR_STALE_MS, WT_MEDIAN_WINDOW);
}

static void log_transition(level_status_t prev, level_status_t now)
{
    if (now == LEVEL_STATUS_NO_DATA) {
        ESP_LOGW(TAG, "sensor data lost: no valid frame for > %d ms (check wiring, RX/TX pins)",
                 CONFIG_WT_SENSOR_STALE_MS);
    } else if (prev == LEVEL_STATUS_NO_DATA) {
        ESP_LOGI(TAG, "sensor data available");
    }
}

static void reporter_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(CONFIG_WT_REPORT_PERIOD_MS);
    TickType_t last_wake = xTaskGetTickCount();
    level_status_t prev_status = LEVEL_STATUS_OK;

    for (;;) {
        /* Fixed-rate schedule: the period does not drift with processing time. */
        xTaskDelayUntil(&last_wake, period);

        a02yyuw_sample_t sample;
        a02yyuw_stats_t stats;
        bool fresh = a02yyuw_get_latest(&sample);
        a02yyuw_get_stats(&stats);

        level_reading_t r = {
            .age_ms = sample.age_ms,
            .frames_ok = stats.frames_ok,
            .checksum_err = stats.checksum_err,
            .sync_skips = stats.sync_skips,
            .uart_overflows = stats.uart_overflows,
            .bytes_rx = stats.bytes_rx,
            .line_errors = stats.line_errors,
        };
        if (!fresh) {
            r.status = LEVEL_STATUS_NO_DATA;
            r.distance_mm = -1;
        } else {
            r.status = sample.in_range ? LEVEL_STATUS_OK : LEVEL_STATUS_OUT_OF_RANGE;
            r.distance_mm = sample.distance_mm;
        }

        if (r.status != prev_status) {
            log_transition(prev_status, r.status);
            prev_status = r.status;
        }

        level_report_publish(&r);
    }
}

void app_main(void)
{
    init_nvs();
    print_banner();

    const a02yyuw_config_t sensor_cfg = {
        .uart_num = (uart_port_t)CONFIG_WT_SENSOR_UART_NUM,
        .rx_gpio = CONFIG_WT_SENSOR_RX_GPIO,
        .tx_gpio = CONFIG_WT_SENSOR_TX_GPIO,
        .stale_ms = CONFIG_WT_SENSOR_STALE_MS,
        .median_window = WT_MEDIAN_WINDOW,
        .task_priority = SENSOR_TASK_PRIO,
        .task_stack = SENSOR_TASK_STACK,
    };
    ESP_ERROR_CHECK(a02yyuw_init(&sensor_cfg));
    ESP_ERROR_CHECK(level_report_init());

    if (xTaskCreate(reporter_task, "wt_report", REPORT_TASK_STACK, NULL, REPORT_TASK_PRIO,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "reporter task creation failed");
        abort();
    }
}
