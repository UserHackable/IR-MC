#pragma once

#include "driver/gpio.h"

/* Classic 38-pin DevKitC-style. Override at compile time if the board differs. */

#ifndef IR_TX_GPIO
#define IR_TX_GPIO GPIO_NUM_18
#endif

#ifndef STATUS_LED_GPIO
#define STATUS_LED_GPIO GPIO_NUM_2
#endif

#ifndef BUTTON_GPIO
#define BUTTON_GPIO GPIO_NUM_0
#endif

#ifndef IR_RX_GPIO
#define IR_RX_GPIO GPIO_NUM_19
#endif

/* Lonely Binary TK64 phototransistor (raw, no demod). ADC1 CH6. */
#ifndef IR_RAW_GPIO
#define IR_RAW_GPIO GPIO_NUM_34
#endif
