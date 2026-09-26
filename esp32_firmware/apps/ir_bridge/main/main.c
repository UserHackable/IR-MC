/*
 * ESP-IDF app: ir_bridge
 *
 * IR-MC — network-addressable IR transceiver:
 *   1. NVS Wi‑Fi STA + DHCP (optional static IP / hostname)
 *   2. UDP LAN announce (role=ir_mc)
 *   3. HTTP :80 control + MQTT named/raw IR transmit via RMT
 *
 *   ./scripts/fw idf nvs-wifi
 *   ./scripts/fw idf upload ir_bridge
 *   ./scripts/fw idf monitor ir_bridge
 */

#include "app.h"
#include "board.h"
#include "catalog.h"
#include "ir_tx.h"
#include "ir_rx.h"
#include "led_sense.h"
#include "ir_raw.h"
#include "net_ctrl.h"
#include "serial_cmd.h"

#include "driver/gpio.h"

#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "ir_bridge";

char g_identity[32];
char g_hostname[32];
char g_ip_str[16];
EventGroupHandle_t g_wifi_events;

#define NVS_NS_WIFI      "wifi"
#define NVS_KEY_SSID     "ssid"
#define NVS_KEY_PASS     "password"
#define NVS_KEY_HOSTNAME "hostname"
#define NVS_KEY_IP       "ip"
#define NVS_KEY_GW       "gateway"
#define NVS_KEY_MASK     "netmask"

static esp_netif_t *s_sta_netif;

static void nvs_get_opt_str(nvs_handle_t h, const char *key, char *out, size_t len)
{
    out[0] = 0;
    size_t n = len;
    if (nvs_get_str(h, key, out, &n) != ESP_OK) {
        out[0] = 0;
    }
}

static void net_apply_from_nvs(esp_netif_t *netif)
{
    nvs_handle_t h;
    char hostname[32] = {0};
    char ip[16] = {0};
    char gw[16] = {0};
    char mask[16] = {0};

    if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_opt_str(h, NVS_KEY_HOSTNAME, hostname, sizeof(hostname));
        nvs_get_opt_str(h, NVS_KEY_IP, ip, sizeof(ip));
        nvs_get_opt_str(h, NVS_KEY_GW, gw, sizeof(gw));
        nvs_get_opt_str(h, NVS_KEY_MASK, mask, sizeof(mask));
        nvs_close(h);
    }

    const char *host = hostname[0] ? hostname : g_identity;
    snprintf(g_hostname, sizeof(g_hostname), "%s", host);
    if (esp_netif_set_hostname(netif, host) != ESP_OK) {
        ESP_LOGW(TAG, "hostname '%s' rejected", host);
    }

    if (!ip[0]) {
        return;
    }

    esp_netif_ip_info_t info = {0};
    if (esp_netif_str_to_ip4(ip, &info.ip) != ESP_OK) {
        ESP_LOGW(TAG, "bad static ip '%s' — DHCP", ip);
        return;
    }
    if (esp_netif_str_to_ip4(gw[0] ? gw : "192.168.72.1", &info.gw) != ESP_OK ||
        esp_netif_str_to_ip4(mask[0] ? mask : "255.255.255.0", &info.netmask) != ESP_OK) {
        ESP_LOGW(TAG, "bad static gateway/mask — DHCP");
        return;
    }

    esp_err_t err = esp_netif_dhcpc_stop(netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "dhcpc_stop: %s", esp_err_to_name(err));
    }
    if (esp_netif_set_ip_info(netif, &info) != ESP_OK) {
        ESP_LOGW(TAG, "set_ip_info failed — DHCP");
        return;
    }

    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    if (esp_netif_str_to_ip4("192.168.72.3", &dns.ip.u_addr.ip4) == ESP_OK) {
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
    }
    if (esp_netif_str_to_ip4("192.168.72.2", &dns.ip.u_addr.ip4) == ESP_OK) {
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns);
    }
    ESP_LOGI(TAG, "static IP %s hostname %s", ip, host);
}

static void build_identity(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(g_identity, sizeof(g_identity),
             "%s-%02x%02x%02x%02x%02x%02x",
             IR_ID_PREFIX,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void announce_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "announce socket failed");
        vTaskDelete(NULL);
        return;
    }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(DISCOVERY_PORT);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    ESP_LOGI(TAG, "UDP announce every %d ms on port %d", ANNOUNCE_MS, DISCOVERY_PORT);

    while (true) {
        xEventGroupWaitBits(g_wifi_events, GOT_IP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "announce");
        cJSON_AddNumberToObject(root, "v", 1);
        cJSON_AddStringToObject(root, "role", ROLE);
        cJSON_AddStringToObject(root, "identity", g_identity);
        cJSON_AddStringToObject(root, "ip", g_ip_str);
        cJSON_AddStringToObject(root, "app", APP_NAME);
        cJSON_AddStringToObject(root, "version", APP_VERSION);
        cJSON_AddNumberToObject(root, "http_port", HTTP_PORT);
        char *payload = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        if (payload) {
            int n = (int)strlen(payload);
            int sent = sendto(sock, payload, n, 0, (struct sockaddr *)&dest, sizeof(dest));
            if (sent < 0) {
                ESP_LOGW(TAG, "announce sendto failed");
            } else {
                ESP_LOGI(TAG, "announce %d B identity=%s ip=%s", sent, g_identity, g_ip_str);
            }
            free(payload);
        }

        vTaskDelay(pdMS_TO_TICKS(ANNOUNCE_MS));
    }
}

static void button_task(void *arg)
{
    (void)arg;
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn);

    int last = 1;
    ESP_LOGI(TAG, "BOOT button (GPIO%d) sends demo/power", (int)BUTTON_GPIO);
    while (true) {
        int now = gpio_get_level(BUTTON_GPIO);
        if (last == 1 && now == 0) {
            vTaskDelay(pdMS_TO_TICKS(30));
            if (gpio_get_level(BUTTON_GPIO) == 0) {
                cJSON *cmd = NULL;
                if (catalog_lookup("demo", "power", &cmd) == ESP_OK) {
                    esp_err_t err = ir_execute_json(cmd, -1);
                    catalog_unlock();
                    ESP_LOGI(TAG, "button demo/power -> %s", esp_err_to_name(err));
                } else {
                    ESP_LOGW(TAG, "button: demo/power not in catalog");
                }
                while (gpio_get_level(BUTTON_GPIO) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            }
        }
        last = now;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void wifi_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        g_ip_str[0] = '\0';
        xEventGroupClearBits(g_wifi_events, GOT_IP_BIT);
        net_ctrl_on_lost_ip();
        ESP_LOGW(TAG, "Wi‑Fi disconnected — reconnect");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(g_ip_str, sizeof(g_ip_str), IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "got IP %s identity=%s hostname=%s", g_ip_str, g_identity, g_hostname);
        xEventGroupSetBits(g_wifi_events, GOT_IP_BIT);
        net_ctrl_on_got_ip();
    }
}

static esp_err_t nvs_load_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_WIFI, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = ssid_len;
    err = nvs_get_str(h, NVS_KEY_SSID, ssid, &len);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }
    len = pass_len;
    err = nvs_get_str(h, NVS_KEY_PASS, pass, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        pass[0] = '\0';
        err = ESP_OK;
    }
    nvs_close(h);
    return err;
}

static void wifi_init_sta(const char *ssid, const char *pass)
{
    g_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    net_apply_from_nvs(s_sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_handler, NULL));

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "STA connecting to SSID '%s' (password not logged)", ssid);
}

void app_main(void)
{
    build_identity();
    ESP_LOGI(TAG, "boot %s %s identity=%s", APP_NAME, APP_VERSION, g_identity);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

#if IR_TX_ENABLE
    ESP_ERROR_CHECK(ir_tx_init());
    serial_cmd_start();
#endif
#if IR_RX_ENABLE
    ESP_ERROR_CHECK(ir_rx_init());
#endif
    ESP_ERROR_CHECK(ir_raw_init());
#if IR_TX_ENABLE
    ESP_ERROR_CHECK(led_sense_init());
#endif
    ESP_ERROR_CHECK(catalog_init());

    char ssid[33] = {0};
    char pass[65] = {0};
    err = nvs_load_wifi(ssid, sizeof(ssid), pass, sizeof(pass));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No Wi‑Fi in NVS (namespace '%s'). Run: ./scripts/fw idf nvs-wifi", NVS_NS_WIFI);
        while (true) {
            gpio_set_level(STATUS_LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(STATUS_LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(800));
        }
    }

    wifi_init_sta(ssid, pass);

    EventBits_t bits = xEventGroupWaitBits(g_wifi_events, GOT_IP_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(60000));
    if (!(bits & GOT_IP_BIT)) {
        ESP_LOGW(TAG, "No DHCP within 60s — HTTP/MQTT wait for IP in background");
    }

    ESP_ERROR_CHECK(net_ctrl_start());
    xTaskCreate(announce_task, "announce", 4096, NULL, 5, NULL);
#if IR_TX_ENABLE
    xTaskCreate(button_task, "button", 3072, NULL, 4, NULL);
#endif

    ESP_LOGI(TAG, "ready — open http://<ip>:%d/", HTTP_PORT);
}
