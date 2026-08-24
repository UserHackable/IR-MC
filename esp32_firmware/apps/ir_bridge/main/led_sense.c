#include "led_sense.h"
#include "board.h"
#include "ir_raw.h"
#include "ir_tx.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "led_sense";

/* ESP32 ADC1 channel 4 is GPIO 32 (silk 32, left header). */
#define SENSE_ADC_CHAN ADC_CHANNEL_4
#define SETTLE_MS      40
#define N_AVG          16

esp_err_t led_sense_init(void)
{
    esp_err_t err = ir_raw_add_channel(SENSE_ADC_CHAN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ir_raw_add_channel CH4: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "LED sense ADC1_CH4 GPIO32");
    return ESP_OK;
}

static int read_mv(void)
{
    int acc = 0;
    for (int i = 0; i < N_AVG; i++) {
        int mv = ir_raw_read_chan_mv(SENSE_ADC_CHAN);
        if (mv < 0) {
            return -1;
        }
        acc += mv;
    }
    return acc / N_AVG;
}

esp_err_t led_sense_run(int *on_mv, int *off_mv)
{
    if (!on_mv || !off_mv) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ir_tx_dc_begin();
    if (err != ESP_OK) {
        return err;
    }

    ir_tx_dc_set(1);
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
    *on_mv = read_mv();

    ir_tx_dc_set(0);
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
    *off_mv = read_mv();

    esp_err_t end = ir_tx_dc_end();
    if (*on_mv < 0 || *off_mv < 0) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GPIO18 on=%d mV off=%d mV (tap; LED+ ~ 2x)", *on_mv, *off_mv);
    return end;
}
