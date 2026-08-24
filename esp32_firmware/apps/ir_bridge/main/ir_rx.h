#pragma once

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#define IR_RX_MAX_TIMINGS 512

typedef struct {
    char protocol[16]; /* nec, samsung, sony, raw */
    uint32_t address;
    uint32_t command;
    int bits;
    uint32_t freq_hz; /* TSOP hides the carrier; filled with a protocol default */
    uint16_t timings[IR_RX_MAX_TIMINGS];
    size_t n_timings;
} ir_frame_t;

esp_err_t ir_rx_init(void);

/* Wait for the next demodulated frame (one remote button). timeout_ms <= 0 uses 8000. */
esp_err_t ir_rx_learn(int timeout_ms, ir_frame_t *out);
