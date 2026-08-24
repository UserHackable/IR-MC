/*
 * ESP-IDF app: tx_probe
 *
 * Plug a TO-92 BJT (2N2222 / 2N3904) through 1 kΩ on each lead into
 * GPIO32, GPIO33, GPIO25. The firmware cycles drive/sense on those pins
 * and prints the pinout (E/B/C) and NPN vs PNP.
 *
 * Those three are consecutive on the left header (USB-C at the top):
 *   7th down = silk 32
 *   8th down = silk 33
 *   9th down = silk 25
 *
 * Do NOT use VP / VN / 34 (header pins 3–5). Those are ADC1 input-only
 * and cannot drive a base.
 *
 *   ./scripts/fw idf upload tx_probe
 *   ./scripts/fw idf monitor tx_probe
 */

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "tx_probe";

#define N_PINS 3
#define SETTLE_MS 3
#define LOOP_MS 2000
#define DIODE_MV_MIN 800
#define SAT_MV_MAX 1200

typedef struct {
    gpio_num_t gpio;
    const char *silk;
    adc_unit_t adc_unit;
    adc_channel_t adc_chan;
} probe_pin_t;

/* Left header, USB-C at top: 7th / 8th / 9th. All can be input *and* output. */
static const probe_pin_t k_pins[N_PINS] = {
    { .gpio = GPIO_NUM_32, .silk = "32", .adc_unit = ADC_UNIT_1, .adc_chan = ADC_CHANNEL_4 },
    { .gpio = GPIO_NUM_33, .silk = "33", .adc_unit = ADC_UNIT_1, .adc_chan = ADC_CHANNEL_5 },
    { .gpio = GPIO_NUM_25, .silk = "25", .adc_unit = ADC_UNIT_2, .adc_chan = ADC_CHANNEL_8 },
};

static adc_oneshot_unit_handle_t s_adc1;
static adc_oneshot_unit_handle_t s_adc2;
static adc_cali_handle_t s_cali1;
static adc_cali_handle_t s_cali2;
static bool s_cali1_ok;
static bool s_cali2_ok;

static bool cali_line(adc_unit_t unit, adc_cali_handle_t *out)
{
    *out = NULL;
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cfg = {
        .unit_id = unit,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cfg, out) == ESP_OK) {
        return true;
    }
#endif
    return false;
}

static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t u1 = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_unit_init_cfg_t u2 = { .unit_id = ADC_UNIT_2 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&u1, &s_adc1));
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&u2, &s_adc2));

    adc_oneshot_chan_cfg_t ch = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1, ADC_CHANNEL_4, &ch));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1, ADC_CHANNEL_5, &ch));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc2, ADC_CHANNEL_8, &ch));

    s_cali1_ok = cali_line(ADC_UNIT_1, &s_cali1);
    s_cali2_ok = cali_line(ADC_UNIT_2, &s_cali2);
}

static int read_mv(int idx)
{
    adc_oneshot_unit_handle_t unit = (k_pins[idx].adc_unit == ADC_UNIT_1) ? s_adc1 : s_adc2;
    adc_cali_handle_t cali = (k_pins[idx].adc_unit == ADC_UNIT_1) ? s_cali1 : s_cali2;
    bool cali_ok = (k_pins[idx].adc_unit == ADC_UNIT_1) ? s_cali1_ok : s_cali2_ok;

    int acc = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0;
        if (adc_oneshot_read(unit, k_pins[idx].adc_chan, &raw) != ESP_OK) {
            return -1;
        }
        int mv = raw;
        if (cali_ok) {
            int v = 0;
            if (adc_cali_raw_to_voltage(cali, raw, &v) == ESP_OK) {
                mv = v;
            }
        }
        acc += mv;
    }
    return acc / 8;
}

static void pin_hiz(int idx)
{
    gpio_reset_pin(k_pins[idx].gpio);
    gpio_set_direction(k_pins[idx].gpio, GPIO_MODE_INPUT);
    gpio_pulldown_dis(k_pins[idx].gpio);
    gpio_pullup_dis(k_pins[idx].gpio);
}

static void all_hiz(void)
{
    for (int i = 0; i < N_PINS; i++) {
        pin_hiz(i);
    }
}

static void pin_out(int idx, int level)
{
    gpio_reset_pin(k_pins[idx].gpio);
    gpio_set_direction(k_pins[idx].gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(k_pins[idx].gpio, level);
}

static void pin_in_pd(int idx)
{
    gpio_reset_pin(k_pins[idx].gpio);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << k_pins[idx].gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

static void pin_in_pu(int idx)
{
    gpio_reset_pin(k_pins[idx].gpio);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << k_pins[idx].gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

/* True if a junction conducts from anode idx -> cathode idx. */
static bool diode_from_to(int from, int to, int *mv_out)
{
    all_hiz();
    pin_out(from, 1);
    pin_in_pd(to);
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
    int mv = read_mv(to);
    int level = gpio_get_level(k_pins[to].gpio);
    if (mv_out) {
        *mv_out = mv;
    }
    all_hiz();
    return (level == 1) || (mv >= DIODE_MV_MIN);
}

/* NPN: base high, emitter low, collector pulled up. Conducting => collector falls. */
static int npn_sat_mv(int b, int e, int c)
{
    all_hiz();
    pin_out(e, 0);
    pin_in_pu(c);
    pin_out(b, 1);
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
    int mv = read_mv(c);
    all_hiz();
    return mv;
}

/* PNP: base low, emitter high, collector pulled down. Conducting => collector rises. */
static int pnp_sat_mv(int b, int e, int c)
{
    all_hiz();
    pin_out(e, 1);
    pin_in_pd(c);
    pin_out(b, 0);
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
    int mv = read_mv(c);
    all_hiz();
    return mv;
}

static const char *role_name(int pin, int e, int b, int c)
{
    if (pin == e) {
        return "E";
    }
    if (pin == b) {
        return "B";
    }
    if (pin == c) {
        return "C";
    }
    return "?";
}

static void scan_once(void)
{
    bool d[N_PINS][N_PINS];
    int dmv[N_PINS][N_PINS];
    memset(d, 0, sizeof(d));
    memset(dmv, 0, sizeof(dmv));

    for (int i = 0; i < N_PINS; i++) {
        for (int j = 0; j < N_PINS; j++) {
            if (i == j) {
                continue;
            }
            d[i][j] = diode_from_to(i, j, &dmv[i][j]);
        }
    }

    int out_count[N_PINS] = {0};
    int in_count[N_PINS] = {0};
    for (int i = 0; i < N_PINS; i++) {
        for (int j = 0; j < N_PINS; j++) {
            if (d[i][j]) {
                out_count[i]++;
                in_count[j]++;
            }
        }
    }

    int npn_base = -1, pnp_base = -1;
    for (int i = 0; i < N_PINS; i++) {
        if (out_count[i] == 2 && in_count[i] == 0) {
            npn_base = i;
        }
        if (in_count[i] == 2 && out_count[i] == 0) {
            pnp_base = i;
        }
    }

    char matrix[96];
    snprintf(matrix, sizeof(matrix),
             "diodes %s->%s=%d/%dmV %s->%s=%d/%dmV %s->%s=%d/%dmV %s->%s=%d/%dmV %s->%s=%d/%dmV %s->%s=%d/%dmV",
             k_pins[0].silk, k_pins[1].silk, (int)d[0][1], dmv[0][1],
             k_pins[0].silk, k_pins[2].silk, (int)d[0][2], dmv[0][2],
             k_pins[1].silk, k_pins[0].silk, (int)d[1][0], dmv[1][0],
             k_pins[1].silk, k_pins[2].silk, (int)d[1][2], dmv[1][2],
             k_pins[2].silk, k_pins[0].silk, (int)d[2][0], dmv[2][0],
             k_pins[2].silk, k_pins[1].silk, (int)d[2][1], dmv[2][1]);
    ESP_LOGI(TAG, "%s", matrix);

    if (npn_base >= 0 && pnp_base < 0) {
        int others[2];
        int n = 0;
        for (int i = 0; i < N_PINS; i++) {
            if (i != npn_base) {
                others[n++] = i;
            }
        }
        int mv_a = npn_sat_mv(npn_base, others[0], others[1]);
        int mv_b = npn_sat_mv(npn_base, others[1], others[0]);
        int e, c, sat;
        if (mv_a <= mv_b) {
            e = others[0];
            c = others[1];
            sat = mv_a;
        } else {
            e = others[1];
            c = others[0];
            sat = mv_b;
        }
        if (sat > SAT_MV_MAX) {
            ESP_LOGW(TAG, "NPN base=GPIO%s but C never saturates (sat=%d mV) — check 1k leads",
                     k_pins[npn_base].silk, sat);
        }
        ESP_LOGI(TAG, "RESULT  NPN  GPIO%s=E  GPIO%s=B  GPIO%s=C  (Vce_sat~%d mV)",
                 k_pins[e].silk, k_pins[npn_base].silk, k_pins[c].silk, sat);
        ESP_LOGI(TAG, "        silk %s=%s  %s=%s  %s=%s",
                 k_pins[0].silk, role_name(0, e, npn_base, c),
                 k_pins[1].silk, role_name(1, e, npn_base, c),
                 k_pins[2].silk, role_name(2, e, npn_base, c));
        return;
    }

    if (pnp_base >= 0 && npn_base < 0) {
        int others[2];
        int n = 0;
        for (int i = 0; i < N_PINS; i++) {
            if (i != pnp_base) {
                others[n++] = i;
            }
        }
        int mv_a = pnp_sat_mv(pnp_base, others[0], others[1]);
        int mv_b = pnp_sat_mv(pnp_base, others[1], others[0]);
        /* Conducting PNP pulls collector *up* toward emitter. Pick the larger mv. */
        int e, c, sat;
        if (mv_a >= mv_b) {
            e = others[0];
            c = others[1];
            sat = mv_a;
        } else {
            e = others[1];
            c = others[0];
            sat = mv_b;
        }
        ESP_LOGI(TAG, "RESULT  PNP  GPIO%s=E  GPIO%s=B  GPIO%s=C  (Vc~%d mV while on)",
                 k_pins[e].silk, k_pins[pnp_base].silk, k_pins[c].silk, sat);
        return;
    }

    if (out_count[0] + out_count[1] + out_count[2] == 0) {
        ESP_LOGI(TAG, "RESULT  nothing — no junctions. Empty socket / open 1k / not plugged in");
        return;
    }
    ESP_LOGW(TAG, "RESULT  unknown pattern (not a single NPN/PNP). Check leads, or try the other TO-92 orientation");
}

void app_main(void)
{
    adc_init();
    all_hiz();
    ESP_LOGI(TAG, "tx_probe transistor identifier");
    ESP_LOGI(TAG, "plug TO-92 via 1k each lead: silk 32, 33, 25 (left header 7/8/9 from USB)");
    ESP_LOGI(TAG, "scanning every %d ms", LOOP_MS);

    while (true) {
        scan_once();
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}
