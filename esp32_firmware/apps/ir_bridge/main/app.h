#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifndef APP_NAME
#define APP_NAME    "ir_bridge"
#endif
#ifndef APP_VERSION
#define APP_VERSION "0.2.0"
#endif
#ifndef ROLE
#define ROLE        "ir_mc"
#endif
#ifndef IR_ID_PREFIX
#define IR_ID_PREFIX "ir-mc"
#endif
#ifndef IR_TX_ENABLE
#define IR_TX_ENABLE 1
#endif
#ifndef IR_RX_ENABLE
#define IR_RX_ENABLE 1
#endif

#define HTTP_PORT      8080
#define DISCOVERY_PORT 3000
#define ANNOUNCE_MS    5000

#define GOT_IP_BIT BIT0

extern char g_identity[32];
extern char g_ip_str[16];
extern EventGroupHandle_t g_wifi_events;
