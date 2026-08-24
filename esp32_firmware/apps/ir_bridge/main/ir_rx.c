#include "ir_rx.h"
#include "board.h"

#include "driver/rmt_rx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <string.h>
#include <strings.h>

static const char *TAG = "ir_rx";

#define IR_RX_RES_HZ        1000000u
#define IR_RX_MAX_SYMBOLS   256
#define IR_RX_MIN_NS        1250
#define IR_RX_MAX_NS        20000000
#define LEARN_DEFAULT_MS    8000

static rmt_channel_handle_t s_rx;
static QueueHandle_t s_done;
static QueueHandle_t s_frames;
static rmt_symbol_word_t s_symbols[IR_RX_MAX_SYMBOLS];

static bool in_range(uint32_t v, uint32_t target, uint32_t pct)
{
    uint32_t d = (target * pct) / 100;
    return v + d >= target && v <= target + d;
}

static bool rx_done_cb(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *edata, void *user)
{
    (void)ch;
    (void)user;
    BaseType_t hp = pdFALSE;
    xQueueSendFromISR(s_done, edata, &hp);
    return hp == pdTRUE;
}

static size_t symbols_to_timings(const rmt_symbol_word_t *sym, size_t nsym,
                                 uint16_t *out, size_t out_max)
{
    /* TSOP idle = HIGH. IR mark = TSOP LOW. Skip a leading idle-high fragment. */
    size_t nt = 0;
    for (size_t i = 0; i < nsym && nt + 1 < out_max; i++) {
        uint32_t d0 = sym[i].duration0;
        uint32_t d1 = sym[i].duration1;
        int l0 = sym[i].level0;
        int l1 = sym[i].level1;
        if (i == 0 && l0 == 1 && d0 < 200) {
            /* tiny leading high: ignore */
        } else if (nt == 0 && l0 == 1) {
            /* start at first low (mark) if possible */
            if (d1) {
                uint32_t v = d1 > 65535 ? 65535 : d1;
                out[nt++] = (uint16_t)v;
            }
            continue;
        }
        if (d0) {
            uint32_t v = d0 > 65535 ? 65535 : d0;
            out[nt++] = (uint16_t)v;
        }
        if (d1 && nt < out_max) {
            uint32_t v = d1 > 65535 ? 65535 : d1;
            out[nt++] = (uint16_t)v;
        }
        (void)l1;
    }
    return nt;
}

static int decode_bits_nec_style(const uint16_t *t, size_t n, int start, uint32_t *bits_out)
{
    uint32_t bits = 0;
    int nb = 0;
    for (size_t i = (size_t)start; i + 1 < n && nb < 32; i += 2) {
        if (!in_range(t[i], 560, 40)) {
            break;
        }
        if (in_range(t[i + 1], 1690, 35)) {
            bits |= (1u << nb);
        } else if (!in_range(t[i + 1], 560, 45)) {
            break;
        }
        nb++;
    }
    *bits_out = bits;
    return nb;
}

static void try_decode(ir_frame_t *f)
{
    snprintf(f->protocol, sizeof(f->protocol), "raw");
    f->address = 0;
    f->command = 0;
    f->bits = 0;
    f->freq_hz = 38000;
    if (f->n_timings < 4) {
        return;
    }

    /* NEC: 9 ms mark, 4.5 ms space */
    if (in_range(f->timings[0], 9000, 25) && in_range(f->timings[1], 4500, 25)) {
        uint32_t bits = 0;
        int nb = decode_bits_nec_style(f->timings, f->n_timings, 2, &bits);
        if (nb >= 32) {
            uint8_t a0 = bits & 0xFF;
            uint8_t a1 = (bits >> 8) & 0xFF;
            uint8_t c0 = (bits >> 16) & 0xFF;
            uint8_t c1 = (bits >> 24) & 0xFF;
            snprintf(f->protocol, sizeof(f->protocol), "nec");
            f->bits = 32;
            f->freq_hz = 38000;
            if ((uint8_t)~a0 == a1) {
                f->address = a0;
            } else {
                f->address = (uint16_t)(a0 | ((uint16_t)a1 << 8));
            }
            f->command = c0;
            (void)c1;
            return;
        }
    }

    /* Samsung: 4.5 / 4.5 header, NEC-like bits */
    if (in_range(f->timings[0], 4500, 25) && in_range(f->timings[1], 4500, 25)) {
        uint32_t bits = 0;
        int nb = decode_bits_nec_style(f->timings, f->n_timings, 2, &bits);
        if (nb >= 32) {
            snprintf(f->protocol, sizeof(f->protocol), "samsung");
            f->bits = 32;
            f->freq_hz = 38000;
            f->address = bits & 0xFFFF;
            f->command = (bits >> 16) & 0xFFFF;
            return;
        }
    }

    /* Sony SIRC: ~2.4 ms mark, ~0.6 ms space, then 12/15/20 bits */
    if (in_range(f->timings[0], 2400, 30) && in_range(f->timings[1], 600, 40)) {
        uint32_t bits = 0;
        int nb = 0;
        for (size_t i = 2; i < f->n_timings && nb < 20; i += 2) {
            uint32_t mark = f->timings[i];
            if (in_range(mark, 1200, 35)) {
                bits |= (1u << nb);
            } else if (!in_range(mark, 600, 40)) {
                break;
            }
            nb++;
        }
        if (nb == 12 || nb == 15 || nb == 20) {
            snprintf(f->protocol, sizeof(f->protocol), "sony");
            f->bits = nb;
            f->freq_hz = 40000;
            f->command = bits & 0x7F;
            f->address = bits >> 7;
            return;
        }
    }
}

static void rx_task(void *arg)
{
    (void)arg;
    rmt_receive_config_t cfg = {
        .signal_range_min_ns = IR_RX_MIN_NS,
        .signal_range_max_ns = IR_RX_MAX_NS,
    };

    ESP_LOGI(TAG, "TSOP learn on GPIO%d (idle high)", (int)IR_RX_GPIO);

    while (true) {
        esp_err_t err = rmt_receive(s_rx, s_symbols, sizeof(s_symbols), &cfg);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "rmt_receive: %s — reset channel", esp_err_to_name(err));
            (void)rmt_disable(s_rx);
            (void)rmt_enable(s_rx);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        rmt_rx_done_event_data_t ed = {0};
        /* Wait for this receive to finish. Restarting rmt_receive while the
         * channel is still RUN leaves it stuck: "channel not in enable state". */
        if (xQueueReceive(s_done, &ed, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (ed.num_symbols < 2) {
            continue;
        }
        ir_frame_t fr = {0};
        fr.n_timings = symbols_to_timings(ed.received_symbols, ed.num_symbols,
                                          fr.timings, IR_RX_MAX_TIMINGS);
        try_decode(&fr);
        ESP_LOGI(TAG, "rx proto=%s addr=%lu cmd=%lu bits=%d n=%u",
                 fr.protocol, (unsigned long)fr.address, (unsigned long)fr.command,
                 fr.bits, (unsigned)fr.n_timings);
        xQueueOverwrite(s_frames, &fr);
    }
}

esp_err_t ir_rx_init(void)
{
    rmt_rx_channel_config_t rx_cfg = {
        .gpio_num = IR_RX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RX_RES_HZ,
        .mem_block_symbols = 64,
        .flags.invert_in = false,
        .flags.with_dma = false,
    };
    esp_err_t err = rmt_new_rx_channel(&rx_cfg, &s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_rx_channel: %s", esp_err_to_name(err));
        return err;
    }

    s_done = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    s_frames = xQueueCreate(1, sizeof(ir_frame_t));
    if (!s_done || !s_frames) {
        return ESP_ERR_NO_MEM;
    }

    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rx_done_cb,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(s_rx, &cbs, NULL));
    ESP_ERROR_CHECK(rmt_enable(s_rx));

    if (xTaskCreate(rx_task, "ir_rx", 4096, NULL, 6, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ir_rx_learn(int timeout_ms, ir_frame_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (timeout_ms <= 0) {
        timeout_ms = LEARN_DEFAULT_MS;
    }
    ir_frame_t dump;
    while (xQueueReceive(s_frames, &dump, 0) == pdTRUE) {
        /* drain stale frames */
    }
    if (xQueueReceive(s_frames, out, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
