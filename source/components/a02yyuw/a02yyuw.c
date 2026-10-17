#include "a02yyuw.h"

#include <inttypes.h>
#include <string.h>

#include "a02yyuw_filter.h"
#include "a02yyuw_parser.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define RX_RING_SIZE   512  /* driver ring buffer; must exceed the 128-byte HW FIFO */
#define EVT_QUEUE_LEN  16
#define READ_CHUNK     64

static const char *TAG = "a02yyuw";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static struct {
    a02yyuw_config_t cfg;
    QueueHandle_t    evt_queue;
    TaskHandle_t     task;

    /* Owned by the RX task only. */
    a02yyuw_parser_t parser;
    a02yyuw_filter_t filter;
    int64_t          last_frame_us;  /* 0 = no frame yet */
    bool             locked;         /* first valid frame seen (never cleared) */
    uint32_t         cks_base;       /* parser counters at lock, 0 before it */
    uint32_t         sync_base;

    /* Shared, guarded by s_lock. */
    bool             have_sample;
    a02yyuw_sample_t latest;
    a02yyuw_stats_t  stats;
} s_ctx;

/* On ESP32 modules GPIO6..11 are wired to the SPI flash; driving them hangs the chip. */
static bool is_flash_gpio(int gpio)
{
#if CONFIG_IDF_TARGET_ESP32
    return gpio >= 6 && gpio <= 11;
#else
    (void)gpio;
    return false;
#endif
}

static void on_frame(uint16_t raw_mm)
{
    int64_t now = esp_timer_get_time();

    /* Do not mix pre-dropout samples into the median after the sensor comes back. */
    if (s_ctx.last_frame_us == 0 ||
        now - s_ctx.last_frame_us > (int64_t)s_ctx.cfg.stale_ms * 1000) {
        a02yyuw_filter_reset(&s_ctx.filter);
    }
    s_ctx.last_frame_us = now;

    uint16_t filtered = a02yyuw_filter_push(&s_ctx.filter, raw_mm);

    taskENTER_CRITICAL(&s_lock);
    s_ctx.latest.raw_mm = raw_mm;
    s_ctx.latest.distance_mm = filtered;
    s_ctx.latest.timestamp_us = now;
    s_ctx.have_sample = true;
    taskEXIT_CRITICAL(&s_lock);
}

static void handle_bytes(const uint8_t *data, size_t len)
{
    bool just_locked = false;

    for (size_t i = 0; i < len; i++) {
        uint16_t mm;
        if (a02yyuw_parser_feed(&s_ctx.parser, data[i], &mm)) {
            /* First valid frame: errors so far came from joining the stream mid-frame (F-33). */
            if (!s_ctx.locked) {
                s_ctx.locked = true;
                s_ctx.cks_base = s_ctx.parser.checksum_err;
                s_ctx.sync_base = s_ctx.parser.sync_skips;
                just_locked = true;
            }
            on_frame(mm);
        }
    }

    uint32_t line_err_discarded = 0;
    uint32_t bytes_rx;

    /* Bases are 0 until lock, so the raw counts are published before it. */
    taskENTER_CRITICAL(&s_lock);
    if (just_locked) {
        line_err_discarded = s_ctx.stats.line_errors;
        s_ctx.stats.line_errors = 0;
    }
    s_ctx.stats.frames_ok = s_ctx.parser.frames_ok;
    s_ctx.stats.checksum_err = s_ctx.parser.checksum_err - s_ctx.cks_base;
    s_ctx.stats.sync_skips = s_ctx.parser.sync_skips - s_ctx.sync_base;
    s_ctx.stats.bytes_rx += (uint32_t)len;
    bytes_rx = s_ctx.stats.bytes_rx;
    taskEXIT_CRITICAL(&s_lock);

    if (just_locked) {
        ESP_LOGI(TAG, "sensor locked after %" PRIu32 " bytes (acquisition discarded: cks_err=%"
                 PRIu32 " sync_skip=%" PRIu32 " line_err=%" PRIu32 ")",
                 bytes_rx, s_ctx.cks_base, s_ctx.sync_base, line_err_discarded);
    }
}

static void rx_task(void *arg)
{
    (void)arg;
    const uart_port_t port = s_ctx.cfg.uart_num;
    uint8_t buf[READ_CHUNK];
    uart_event_t evt;

    for (;;) {
        if (xQueueReceive(s_ctx.evt_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (evt.type) {
        case UART_DATA: {
            size_t remaining = evt.size;
            while (remaining > 0) {
                size_t want = remaining < sizeof(buf) ? remaining : sizeof(buf);
                int n = uart_read_bytes(port, buf, (uint32_t)want, pdMS_TO_TICKS(20));
                if (n <= 0) {
                    break;
                }
                handle_bytes(buf, (size_t)n);
                remaining -= (size_t)n;
            }
            break;
        }

        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
            ESP_LOGW(TAG, "UART %s overflow, input flushed",
                     evt.type == UART_FIFO_OVF ? "FIFO" : "ring buffer");
            uart_flush_input(port);
            xQueueReset(s_ctx.evt_queue);
            a02yyuw_parser_reset(&s_ctx.parser);
            taskENTER_CRITICAL(&s_lock);
            s_ctx.stats.uart_overflows++;
            taskEXIT_CRITICAL(&s_lock);
            break;

        case UART_FRAME_ERR:
        case UART_PARITY_ERR:
            taskENTER_CRITICAL(&s_lock);
            s_ctx.stats.line_errors++;
            taskEXIT_CRITICAL(&s_lock);
            break;

        default:
            break;
        }
    }
}

esp_err_t a02yyuw_init(const a02yyuw_config_t *cfg)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "cfg is NULL");
    ESP_RETURN_ON_FALSE(s_ctx.task == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialised");
    ESP_RETURN_ON_FALSE(cfg->uart_num > UART_NUM_0 && cfg->uart_num < UART_NUM_MAX,
                        ESP_ERR_INVALID_ARG, TAG,
                        "UART%d not usable (UART0 is reserved for the console)",
                        (int)cfg->uart_num);
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(cfg->rx_gpio) && !is_flash_gpio(cfg->rx_gpio),
                        ESP_ERR_INVALID_ARG, TAG,
                        "RX GPIO%d is not a usable input (non-existent or SPI flash pin)",
                        cfg->rx_gpio);
    ESP_RETURN_ON_FALSE(cfg->tx_gpio < 0 ||
                        (GPIO_IS_VALID_OUTPUT_GPIO(cfg->tx_gpio) && !is_flash_gpio(cfg->tx_gpio)),
                        ESP_ERR_INVALID_ARG, TAG,
                        "TX GPIO%d is not a usable output (input-only, non-existent or SPI flash pin)",
                        cfg->tx_gpio);
    ESP_RETURN_ON_FALSE(cfg->tx_gpio != cfg->rx_gpio, ESP_ERR_INVALID_ARG, TAG,
                        "RX and TX GPIO must differ");

    s_ctx.cfg = *cfg;
    a02yyuw_parser_init(&s_ctx.parser);
    a02yyuw_filter_init(&s_ctx.filter, cfg->median_window);
    s_ctx.last_frame_us = 0;

    const uart_config_t uart_cfg = {
        .baud_rate = A02YYUW_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(cfg->uart_num, RX_RING_SIZE, 0, EVT_QUEUE_LEN,
                                            &s_ctx.evt_queue, 0),
                        TAG, "uart_driver_install failed");
    ESP_GOTO_ON_ERROR(uart_param_config(cfg->uart_num, &uart_cfg), fail, TAG,
                      "uart_param_config failed");
    ESP_GOTO_ON_ERROR(uart_set_pin(cfg->uart_num,
                                   cfg->tx_gpio < 0 ? UART_PIN_NO_CHANGE : cfg->tx_gpio,
                                   cfg->rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                      fail, TAG, "uart_set_pin failed");

    /* Keep RX idle-high if the sensor is unplugged, to avoid decoding noise. */
    if (gpio_pullup_en((gpio_num_t)cfg->rx_gpio) != ESP_OK) {
        ESP_LOGW(TAG, "no internal pull-up on GPIO%d", cfg->rx_gpio);
    }

    if (xTaskCreate(rx_task, "a02yyuw_rx", cfg->task_stack, NULL, cfg->task_priority,
                    &s_ctx.task) != pdPASS) {
        ret = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "RX task creation failed");
        goto fail;
    }

    ESP_LOGI(TAG, "UART%d @%d 8N1, rx=GPIO%d tx=GPIO%d, median_window=%u",
             (int)cfg->uart_num, A02YYUW_BAUD_RATE, cfg->rx_gpio, cfg->tx_gpio,
             (unsigned)s_ctx.filter.window);
    return ESP_OK;

fail:
    uart_driver_delete(cfg->uart_num);
    s_ctx.evt_queue = NULL;
    s_ctx.task = NULL;
    return ret;
}

bool a02yyuw_get_latest(a02yyuw_sample_t *out)
{
    if (out == NULL) {
        return false;
    }

    bool have;
    taskENTER_CRITICAL(&s_lock);
    have = s_ctx.have_sample;
    *out = s_ctx.latest;
    taskEXIT_CRITICAL(&s_lock);

    if (!have) {
        memset(out, 0, sizeof(*out));
        out->age_ms = -1;
        return false;
    }

    int64_t age_us = esp_timer_get_time() - out->timestamp_us;
    int64_t age_ms = age_us / 1000;
    out->age_ms = age_ms > INT32_MAX ? INT32_MAX : (int32_t)age_ms;
    out->in_range = a02yyuw_distance_in_range(out->distance_mm);
    return age_us <= (int64_t)s_ctx.cfg.stale_ms * 1000;
}

void a02yyuw_get_stats(a02yyuw_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    *out = s_ctx.stats;
    taskEXIT_CRITICAL(&s_lock);
}
