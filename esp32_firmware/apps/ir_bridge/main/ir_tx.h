#pragma once

#include "esp_err.h"
#include "cJSON.h"

#include <stddef.h>
#include <stdint.h>

esp_err_t ir_tx_init(void);

/* Steal GPIO18 from RMT as a plain DC output (transistor on/off, no carrier). */
esp_err_t ir_tx_dc_begin(void);
void ir_tx_dc_set(int on);
esp_err_t ir_tx_dc_end(void);

/* timings: mark, space, mark, space, ... microseconds. Odd last value is a trailing mark. */
esp_err_t ir_tx_send_raw(uint32_t freq_hz, const uint16_t *timings, size_t count,
                         int repeat, int gap_ms);

esp_err_t ir_tx_send_nec(uint16_t address, uint16_t command, uint32_t freq_hz, int repeat);
esp_err_t ir_tx_send_samsung(uint16_t address, uint16_t command, uint32_t freq_hz, int repeat);
esp_err_t ir_tx_send_sony(uint16_t address, uint16_t command, int bits, uint32_t freq_hz, int repeat);

/*
 * JSON command object. Recognized shapes:
 *   {"protocol":"nec","address":4,"command":8,"freq":38000,"repeat":2}
 *   {"protocol":"samsung","address":57505,"command":57926}
 *   {"protocol":"sony","address":1,"command":21,"bits":12,"freq":40000,"repeat":3}
 *   {"protocol":"raw","freq":38000,"timings":[9000,4500,...],"repeat":1}
 *
 * repeat_override < 0 uses the JSON/default repeat.
 */
esp_err_t ir_execute_json(const cJSON *cmd, int repeat_override);
