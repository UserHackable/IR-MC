/*
 * Brief GPIO18 pulse for a visible-LED transistor test.
 *
 *   3V3 ── 100 Ω ── blue LED (+) ── LED (−) ── collector
 *   GPIO18 ── 330 Ω ── base
 *   emitter ── GND
 *
 * Blue Vf is close to 3.3 V, so the LED will be dimmer than red/green.
 * Do not omit the 100 Ω; do not use 5 V on a 3.3 V-only test.
 *
 *   ./scripts/fw idf upload tx_pulse
 *   ./scripts/fw idf monitor tx_pulse
 */

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "tx_pulse";

#define DRIVE_GPIO      GPIO_NUM_18
#define PWM_HZ          1000
#define PWM_DUTY_PCT    40
#define BURST_MS        120
#define GAP_MS          880
#define LEDC_RES        LEDC_TIMER_10_BIT

void app_main(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_RES,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t ch = {
        .gpio_num = DRIVE_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    const uint32_t duty_on = (1024 * PWM_DUTY_PCT) / 100;
    ESP_LOGI(TAG, "GPIO%d  %d Hz PWM  %d%%  %d ms burst / %d ms idle",
             (int)DRIVE_GPIO, PWM_HZ, PWM_DUTY_PCT, BURST_MS, GAP_MS);

    while (true) {
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty_on));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
        ESP_LOGI(TAG, "pulse");
        vTaskDelay(pdMS_TO_TICKS(BURST_MS));

        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
        vTaskDelay(pdMS_TO_TICKS(GAP_MS));
    }
}
