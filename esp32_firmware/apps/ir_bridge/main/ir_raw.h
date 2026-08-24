#pragma once

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

typedef struct {
    int min_mv;
    int max_mv;
    int mean_mv;
    int n;
} ir_raw_stats_t;

esp_err_t ir_raw_init(void);
esp_err_t ir_raw_add_channel(adc_channel_t chan);
int ir_raw_read_mv(void);
int ir_raw_read_chan_mv(adc_channel_t chan);
void ir_raw_reset_stats(void);
void ir_raw_poll(void);
void ir_raw_get_stats(ir_raw_stats_t *out);
