#pragma once

#include "esp_err.h"

esp_err_t led_sense_init(void);

/* GPIO18 DC on, ADC1_CH4 (GPIO32), then GPIO18 off and sample again. */
esp_err_t led_sense_run(int *on_mv, int *off_mv);
