#include "ir_raw.h"
#include "board.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "ir_raw";

/* GPIO 34 = ADC1 channel 6 on classic ESP32. */
#define RAW_ADC_UNIT ADC_UNIT_1
#define RAW_ADC_CHAN ADC_CHANNEL_6

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok;
static ir_raw_stats_t s_st;
static int64_t s_sum;

esp_err_t ir_raw_init(void)
{
    adc_oneshot_unit_init_cfg_t u = { .unit_id = RAW_ADC_UNIT };
    esp_err_t err = adc_oneshot_new_unit(&u, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit: %s", esp_err_to_name(err));
        return err;
    }
    adc_oneshot_chan_cfg_t ch = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    err = adc_oneshot_config_channel(s_adc, RAW_ADC_CHAN, &ch);
    if (err != ESP_OK) {
        return err;
    }
    s_cali = NULL;
    s_cali_ok = false;
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cfg = {
        .unit_id = RAW_ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cfg, &s_cali) == ESP_OK) {
        s_cali_ok = true;
    }
#endif
    ir_raw_reset_stats();
    ESP_LOGI(TAG, "TK64 analog GPIO%d ADC1_CH6 cali=%s", (int)IR_RAW_GPIO,
             s_cali_ok ? "yes" : "no");
    return ESP_OK;
}

esp_err_t ir_raw_add_channel(adc_channel_t chan)
{
    if (!s_adc) {
        return ESP_ERR_INVALID_STATE;
    }
    adc_oneshot_chan_cfg_t ch = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    return adc_oneshot_config_channel(s_adc, chan, &ch);
}

int ir_raw_read_chan_mv(adc_channel_t chan)
{
    if (!s_adc) {
        return -1;
    }
    int raw = 0;
    if (adc_oneshot_read(s_adc, chan, &raw) != ESP_OK) {
        return -1;
    }
    if (s_cali_ok) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) {
            return mv;
        }
    }
    return raw;
}

int ir_raw_read_mv(void)
{
    return ir_raw_read_chan_mv(RAW_ADC_CHAN);
}

void ir_raw_reset_stats(void)
{
    s_st.min_mv = 99999;
    s_st.max_mv = -1;
    s_st.mean_mv = 0;
    s_st.n = 0;
    s_sum = 0;
}

void ir_raw_poll(void)
{
    int mv = ir_raw_read_mv();
    if (mv < 0) {
        return;
    }
    if (mv < s_st.min_mv) {
        s_st.min_mv = mv;
    }
    if (mv > s_st.max_mv) {
        s_st.max_mv = mv;
    }
    s_sum += mv;
    s_st.n++;
}

void ir_raw_get_stats(ir_raw_stats_t *out)
{
    if (!out) {
        return;
    }
    *out = s_st;
    if (s_st.n > 0) {
        out->mean_mv = (int)(s_sum / s_st.n);
    } else {
        out->min_mv = 0;
        out->max_mv = 0;
        out->mean_mv = 0;
    }
}
