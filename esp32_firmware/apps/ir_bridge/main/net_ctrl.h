#pragma once

#include "esp_err.h"

esp_err_t net_ctrl_start(void);
void net_ctrl_on_got_ip(void);
void net_ctrl_on_lost_ip(void);
