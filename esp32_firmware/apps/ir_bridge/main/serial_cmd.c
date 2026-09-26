#include "serial_cmd.h"

#include "app.h"
#include "ir_tx.h"

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ir_serial";

static void write_ack(cJSON *root)
{
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return;
    }
    printf("IRACK %s\n", payload);
    fflush(stdout);
    free(payload);
}

static void ack_ok(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "tx", true);
    write_ack(root);
}

static void ack_err(const char *error)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", error ? error : "fail");
    write_ack(root);
}

static void handle_line(char *line)
{
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line != '{') {
        return;
    }

    cJSON *root = cJSON_Parse(line);
    if (!root) {
        ack_err("json");
        return;
    }

    const cJSON *op = cJSON_GetObjectItemCaseSensitive(root, "op");
    if (cJSON_IsString(op) && op->valuestring && strcmp(op->valuestring, "status") == 0) {
        cJSON *ack = cJSON_CreateObject();
        cJSON_AddBoolToObject(ack, "ok", true);
        cJSON_AddBoolToObject(ack, "tx", true);
        cJSON_AddStringToObject(ack, "identity", g_identity);
        cJSON_Delete(root);
        write_ack(ack);
        return;
    }

    esp_err_t err = ir_execute_json(root, -1);
    cJSON_Delete(root);
    if (err == ESP_OK) {
        ack_ok();
    } else {
        ack_err(esp_err_to_name(err));
    }
}

static void serial_cmd_task(void *arg)
{
    char line[768];
    (void)arg;

    setvbuf(stdin, NULL, _IONBF, 0);
    esp_err_t inst = uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
    if (inst != ESP_OK && inst != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(inst));
    }
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_LF);
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_LF);
    ESP_LOGI(TAG, "USB commands on UART (one JSON object per line)");

    for (;;) {
        if (!fgets(line, sizeof(line), stdin)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (n > 0) {
            handle_line(line);
        }
    }
}

void serial_cmd_start(void)
{
    if (xTaskCreate(serial_cmd_task, "ir_serial", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "serial task create failed");
    }
}
