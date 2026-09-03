#include "ir_tx.h"
#include "board.h"
#include "ir_raw.h"

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "ir_tx";

#define IR_RESOLUTION_HZ 1000000u /* 1 tick = 1 us */
#define IR_MAX_SYMBOLS   512
#define IR_MAX_TIMINGS   512
#define IR_DUR_MAX       32767u

static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_encoder;
static SemaphoreHandle_t s_lock;
static uint32_t s_carrier_hz;
static float s_duty = 0.33f; /* typical remote; ceiling bounce needs the extra light */
static float s_applied_duty;
static rmt_symbol_word_t s_symbols[IR_MAX_SYMBOLS];

static void led_set(int on)
{
    gpio_set_level(STATUS_LED_GPIO, on ? 1 : 0);
}

static uint32_t clamp_dur(uint32_t us)
{
    if (us == 0) {
        return 1;
    }
    if (us > IR_DUR_MAX) {
        return IR_DUR_MAX;
    }
    return us;
}

static size_t timings_to_symbols(const uint16_t *t, size_t n, rmt_symbol_word_t *out, size_t out_max)
{
    size_t ns = 0;
    size_t i = 0;

    while (i < n && ns < out_max) {
        uint32_t mark = t[i++];
        uint32_t space = (i < n) ? t[i++] : 0;

        while ((mark > 0 || space > 0) && ns < out_max) {
            uint32_t m = mark > IR_DUR_MAX ? IR_DUR_MAX : mark;
            uint32_t s;
            if (mark > IR_DUR_MAX) {
                /* Finish the long mark before emitting space. */
                s = 0;
            } else {
                s = space > IR_DUR_MAX ? IR_DUR_MAX : space;
            }
            out[ns].level0 = 1;
            out[ns].duration0 = clamp_dur(m);
            out[ns].level1 = 0;
            out[ns].duration1 = s ? clamp_dur(s) : 1;
            ns++;
            mark -= m;
            if (s) {
                space -= s;
            }
        }
    }
    return ns;
}

void ir_tx_set_duty(float duty)
{
    if (duty < 0.05f) {
        duty = 0.05f;
    }
    if (duty > 0.50f) {
        duty = 0.50f;
    }
    s_duty = duty;
}

float ir_tx_get_duty(void)
{
    return s_duty;
}

static esp_err_t apply_carrier(uint32_t freq_hz)
{
    if (freq_hz == s_carrier_hz && s_duty == s_applied_duty) {
        return ESP_OK;
    }
    if (freq_hz == 0) {
        esp_err_t err = rmt_apply_carrier(s_chan, NULL);
        if (err == ESP_OK) {
            s_carrier_hz = 0;
            s_applied_duty = 0;
        }
        return err;
    }
    rmt_carrier_config_t carrier = {
        .frequency_hz = freq_hz,
        .duty_cycle = s_duty,
        .flags.polarity_active_low = false,
    };
    esp_err_t err = rmt_apply_carrier(s_chan, &carrier);
    if (err == ESP_OK) {
        s_carrier_hz = freq_hz;
        s_applied_duty = s_duty;
    }
    return err;
}

static size_t append_space(rmt_symbol_word_t *out, size_t ns, size_t out_max, uint32_t us)
{
    /* Idle-low gap encoded in RMT so inter-frame spacing is microsecond-accurate.
     * vTaskDelay() at the default 10 ms tick was collapsing a 25 ms Sony gap to
     * ~11 ms, which glued frames together on the local TSOP (and the TV). */
    while (us > 0 && ns < out_max) {
        uint32_t chunk = us > IR_DUR_MAX ? IR_DUR_MAX : us;
        out[ns].level0 = 0;
        out[ns].duration0 = clamp_dur(chunk);
        out[ns].level1 = 0;
        out[ns].duration1 = 1;
        ns++;
        us -= chunk;
    }
    return ns;
}

esp_err_t ir_tx_init(void)
{
    gpio_config_t led = {
        .pin_bit_mask = 1ULL << STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led);
    led_set(0);

    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = IR_TX_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = IR_RESOLUTION_HZ,
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = false,
    };
    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &s_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel: %s", esp_err_to_name(err));
        return err;
    }

    rmt_copy_encoder_config_t copy_cfg = {};
    err = rmt_new_copy_encoder(&copy_cfg, &s_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_copy_encoder: %s", esp_err_to_name(err));
        return err;
    }

    err = rmt_enable(s_chan);
    if (err != ESP_OK) {
        return err;
    }

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    err = apply_carrier(38000);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "default carrier failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "RMT TX on GPIO%d, 1 us/tick", (int)IR_TX_GPIO);
    return ESP_OK;
}

esp_err_t ir_tx_dc_begin(void)
{
    if (!s_lock || !s_chan) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = rmt_disable(s_chan);
    if (err != ESP_OK) {
        xSemaphoreGive(s_lock);
        return err;
    }
    /* Drive the pad from the GPIO out register, not RMT. Do not gpio_reset_pin:
     * that enables a pull-up and leaves the transistor on. */
    gpio_set_direction(IR_TX_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(IR_TX_GPIO, GPIO_FLOATING);
    esp_rom_gpio_connect_out_signal(IR_TX_GPIO, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_level(IR_TX_GPIO, 0);
    led_set(0);
    return ESP_OK;
}

void ir_tx_dc_set(int on)
{
    gpio_set_level(IR_TX_GPIO, on ? 1 : 0);
    led_set(on);
}

esp_err_t ir_tx_dc_end(void)
{
    gpio_set_level(IR_TX_GPIO, 0);
    led_set(0);
    gpio_set_pull_mode(IR_TX_GPIO, GPIO_FLOATING);
    /* First TX channel is 0 on this app. Idle level is 0 (LED off). */
    esp_rom_gpio_connect_out_signal(IR_TX_GPIO, RMT_SIG_OUT0_IDX, false, false);
    esp_err_t err = rmt_enable(s_chan);
    if (err == ESP_OK && s_encoder) {
        rmt_symbol_word_t idle = {
            .duration0 = 2,
            .level0 = 0,
            .duration1 = 2,
            .level1 = 0,
        };
        rmt_transmit_config_t tx_cfg = {
            .loop_count = 0,
            .flags.eot_level = 0,
        };
        (void)rmt_transmit(s_chan, s_encoder, &idle, sizeof(idle), &tx_cfg);
        (void)rmt_tx_wait_all_done(s_chan, pdMS_TO_TICKS(50));
    }
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
    return err;
}

esp_err_t ir_tx_send_raw(uint32_t freq_hz, const uint16_t *timings, size_t count,
                         int repeat, int gap_ms)
{
    if (!timings || count == 0 || count > IR_MAX_TIMINGS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (repeat < 1) {
        repeat = 1;
    }
    if (gap_ms < 0) {
        gap_ms = 100;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    led_set(1);
    esp_err_t err = apply_carrier(freq_hz);
    if (err != ESP_OK) {
        led_set(0);
        xSemaphoreGive(s_lock);
        return err;
    }

    size_t ns = timings_to_symbols(timings, count, s_symbols, IR_MAX_SYMBOLS);
    if (ns == 0) {
        led_set(0);
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_SIZE;
    }
    if (repeat > 1 && gap_ms > 0) {
        ns = append_space(s_symbols, ns, IR_MAX_SYMBOLS, (uint32_t)gap_ms * 1000u);
    }

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };

    for (int r = 0; r < repeat; r++) {
        err = rmt_transmit(s_chan, s_encoder, s_symbols, ns * sizeof(rmt_symbol_word_t), &tx_cfg);
        if (err != ESP_OK) {
            break;
        }
        err = rmt_tx_wait_all_done(s_chan, pdMS_TO_TICKS(2000));
        if (err != ESP_OK) {
            break;
        }
        ir_raw_poll();
    }

    led_set(0);
    xSemaphoreGive(s_lock);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "sent %u timings freq=%lu repeat=%d duty=%.2f gap_ms=%d",
                 (unsigned)count, (unsigned long)freq_hz, repeat, (double)s_duty, gap_ms);
    } else {
        ESP_LOGE(TAG, "transmit failed: %s", esp_err_to_name(err));
    }
    return err;
}

static void nec_bit(uint16_t *t, size_t *n, int one)
{
    t[(*n)++] = 560;
    t[(*n)++] = one ? 1690 : 560;
}

static void nec_byte_lsb(uint16_t *t, size_t *n, uint8_t v)
{
    for (int i = 0; i < 8; i++) {
        nec_bit(t, n, (v >> i) & 1);
    }
}

esp_err_t ir_tx_send_nec(uint16_t address, uint16_t command, uint32_t freq_hz, int repeat)
{
    uint16_t t[2 + 32 * 2 + 2];
    size_t n = 0;
    t[n++] = 9000;
    t[n++] = 4500;

    if (address > 0xFF) {
        nec_byte_lsb(t, &n, (uint8_t)(address & 0xFF));
        nec_byte_lsb(t, &n, (uint8_t)((address >> 8) & 0xFF));
    } else {
        nec_byte_lsb(t, &n, (uint8_t)address);
        nec_byte_lsb(t, &n, (uint8_t)~address);
    }
    nec_byte_lsb(t, &n, (uint8_t)command);
    nec_byte_lsb(t, &n, (uint8_t)~command);
    t[n++] = 560; /* trailing mark */

    if (freq_hz == 0) {
        freq_hz = 38000;
    }
    return ir_tx_send_raw(freq_hz, t, n, repeat, 110);
}

esp_err_t ir_tx_send_samsung(uint16_t address, uint16_t command, uint32_t freq_hz, int repeat)
{
    uint16_t t[2 + 32 * 2 + 2];
    size_t n = 0;
    t[n++] = 4500;
    t[n++] = 4500;
    uint32_t word = ((uint32_t)command << 16) | address;
    for (int i = 0; i < 32; i++) {
        nec_bit(t, &n, (int)((word >> i) & 1));
    }
    t[n++] = 560;
    if (freq_hz == 0) {
        freq_hz = 38000;
    }
    return ir_tx_send_raw(freq_hz, t, n, repeat, 110);
}

esp_err_t ir_tx_send_sony(uint16_t address, uint16_t command, int bits, uint32_t freq_hz, int repeat)
{
    if (bits != 12 && bits != 15 && bits != 20) {
        bits = 12;
    }
    int cmd_bits = 7;
    int addr_bits = bits - cmd_bits;
    uint16_t t[2 + 20 * 2 + 2];
    size_t n = 0;
    t[n++] = 2400;
    t[n++] = 600;
    for (int i = 0; i < cmd_bits; i++) {
        t[n++] = ((command >> i) & 1) ? 1200 : 600;
        t[n++] = 600;
    }
    for (int i = 0; i < addr_bits; i++) {
        t[n++] = ((address >> i) & 1) ? 1200 : 600;
        t[n++] = 600;
    }
    if (freq_hz == 0) {
        freq_hz = 40000;
    }
    if (repeat < 1) {
        repeat = 3; /* SIRC is typically sent three times */
    }
    return ir_tx_send_raw(freq_hz, t, n, repeat, 25);
}

static int json_int(const cJSON *obj, const char *key, int fallback)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(n)) {
        return n->valueint;
    }
    return fallback;
}

static uint32_t json_u32(const cJSON *obj, const char *key, uint32_t fallback)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(n)) {
        return (uint32_t)n->valuedouble;
    }
    return fallback;
}

static const char *json_str(const cJSON *obj, const char *key, const char *fallback)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(n) && n->valuestring) {
        return n->valuestring;
    }
    return fallback;
}

esp_err_t ir_execute_json(const cJSON *cmd, int repeat_override)
{
    if (!cJSON_IsObject(cmd)) {
        return ESP_ERR_INVALID_ARG;
    }

    int repeat = json_int(cmd, "repeat", 1);
    if (repeat_override >= 0) {
        repeat = repeat_override;
    }
    uint32_t freq = json_u32(cmd, "freq", 0);
    const char *proto = json_str(cmd, "protocol", NULL);
    float prev_duty = ir_tx_get_duty();
    bool duty_override = false;
    const cJSON *duty_j = cJSON_GetObjectItemCaseSensitive(cmd, "duty");
    if (cJSON_IsNumber(duty_j)) {
        ir_tx_set_duty((float)duty_j->valuedouble);
        duty_override = true;
    }

    esp_err_t err = ESP_ERR_INVALID_ARG;
    const cJSON *timings = cJSON_GetObjectItemCaseSensitive(cmd, "timings");
    if (timings && cJSON_IsArray(timings) && (!proto || strcasecmp(proto, "raw") == 0)) {
        int n = cJSON_GetArraySize(timings);
        if (n <= 0 || n > IR_MAX_TIMINGS) {
            err = ESP_ERR_INVALID_SIZE;
        } else {
            uint16_t *buf = calloc((size_t)n, sizeof(uint16_t));
            if (!buf) {
                err = ESP_ERR_NO_MEM;
            } else {
                err = ESP_OK;
                for (int i = 0; i < n; i++) {
                    const cJSON *el = cJSON_GetArrayItem(timings, i);
                    if (!cJSON_IsNumber(el) || el->valuedouble < 0 || el->valuedouble > 65535) {
                        err = ESP_ERR_INVALID_ARG;
                        break;
                    }
                    buf[i] = (uint16_t)el->valuedouble;
                }
                if (err == ESP_OK) {
                    if (freq == 0) {
                        freq = 38000;
                    }
                    int gap = json_int(cmd, "gap_ms", 100);
                    err = ir_tx_send_raw(freq, buf, (size_t)n, repeat, gap);
                }
                free(buf);
            }
        }
    } else if (!proto) {
        err = ESP_ERR_INVALID_ARG;
    } else {
        uint16_t address = (uint16_t)json_u32(cmd, "address", 0);
        uint16_t command = (uint16_t)json_u32(cmd, "command", 0);
        if (strcasecmp(proto, "nec") == 0) {
            err = ir_tx_send_nec(address, command, freq, repeat);
        } else if (strcasecmp(proto, "samsung") == 0) {
            err = ir_tx_send_samsung(address, command, freq, repeat);
        } else if (strcasecmp(proto, "sony") == 0 || strcasecmp(proto, "sirc") == 0) {
            int bits = json_int(cmd, "bits", 12);
            err = ir_tx_send_sony(address, command, bits, freq, repeat);
        } else {
            err = ESP_ERR_NOT_SUPPORTED;
        }
    }
    if (duty_override) {
        ir_tx_set_duty(prev_duty);
    }
    return err;
}
