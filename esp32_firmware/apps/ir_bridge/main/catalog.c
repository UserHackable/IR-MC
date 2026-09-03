#include "catalog.h"

#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "catalog";

#define CATALOG_PATH "/spiffs/codes.json"

/*
 * Compiled-in fallback. Address/command values match the ESP-IDF NEC example
 * so two boards can test TX/RX against each other. Replace with real codes
 * via PUT /commands or by writing this file on SPIFFS.
 */
static const char *k_defaults =
    "{"
    "\"devices\":{"
    "\"demo\":{"
    "\"power\":{\"protocol\":\"nec\",\"address\":4,\"command\":8,\"freq\":38000,\"repeat\":2},"
    "\"vol_up\":{\"protocol\":\"nec\",\"address\":4,\"command\":2,\"freq\":38000},"
    "\"vol_down\":{\"protocol\":\"nec\",\"address\":4,\"command\":3,\"freq\":38000}"
    "},"
    "\"sceptre\":{"
    "\"power\":{\"protocol\":\"sony\",\"address\":1,\"command\":21,\"bits\":12,\"freq\":40000,\"repeat\":5},"
    "\"info\":{\"protocol\":\"sony\",\"address\":1,\"command\":60,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"digit_1\":{\"protocol\":\"sony\",\"address\":1,\"command\":0,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"digit_2\":{\"protocol\":\"sony\",\"address\":1,\"command\":1,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"digit_3\":{\"protocol\":\"sony\",\"address\":1,\"command\":2,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"digit_4\":{\"protocol\":\"sony\",\"address\":1,\"command\":3,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"digit_5\":{\"protocol\":\"sony\",\"address\":1,\"command\":4,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"digit_6\":{\"protocol\":\"sony\",\"address\":1,\"command\":5,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"digit_7\":{\"protocol\":\"sony\",\"address\":1,\"command\":6,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"digit_8\":{\"protocol\":\"sony\",\"address\":1,\"command\":7,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"digit_9\":{\"protocol\":\"sony\",\"address\":1,\"command\":8,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"dash\":{\"protocol\":\"sony\",\"address\":1,\"command\":32,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"digit_0\":{\"protocol\":\"sony\",\"address\":1,\"command\":9,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"last\":{\"protocol\":\"sony\",\"address\":1,\"command\":59,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"vol_up\":{\"protocol\":\"sony\",\"address\":1,\"command\":18,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"vol_down\":{\"protocol\":\"sony\",\"address\":1,\"command\":19,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"ch_up\":{\"protocol\":\"sony\",\"address\":1,\"command\":16,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"ch_down\":{\"protocol\":\"sony\",\"address\":1,\"command\":17,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"up\":{\"protocol\":\"sony\",\"address\":1,\"command\":116,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"down\":{\"protocol\":\"sony\",\"address\":1,\"command\":117,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"left\":{\"protocol\":\"sony\",\"address\":1,\"command\":52,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"right\":{\"protocol\":\"sony\",\"address\":1,\"command\":51,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"enter\":{\"protocol\":\"sony\",\"address\":1,\"command\":11,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"menu\":{\"protocol\":\"sony\",\"address\":1,\"command\":96,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"guide\":{\"protocol\":\"sony\",\"address\":1,\"command\":101,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"exit\":{\"protocol\":\"sony\",\"address\":1,\"command\":22,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"source\":{\"protocol\":\"sony\",\"address\":1,\"command\":37,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"cc\":{\"protocol\":\"sony\",\"address\":1,\"command\":35,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"mute\":{\"protocol\":\"sony\",\"address\":1,\"command\":20,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"fav\":{\"protocol\":\"sony\",\"address\":1,\"command\":24,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"fav_add_del\":{\"protocol\":\"sony\",\"address\":1,\"command\":36,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"sleep\":{\"protocol\":\"sony\",\"address\":1,\"command\":54,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"usb\":{\"protocol\":\"sony\",\"address\":1,\"command\":99,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"ypbpr\":{\"protocol\":\"sony\",\"address\":1,\"command\":69,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"hdmi\":{\"protocol\":\"sony\",\"address\":1,\"command\":57,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"power2\":{\"protocol\":\"sony\",\"address\":1,\"command\":109,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"color_a\":{\"protocol\":\"sony\",\"address\":1,\"command\":122,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"color_b\":{\"protocol\":\"sony\",\"address\":1,\"command\":123,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"color_c\":{\"protocol\":\"sony\",\"address\":1,\"command\":124,\"bits\":12,\"freq\":40000,\"repeat\":3},"
    "\"color_d\":{\"protocol\":\"sony\",\"address\":1,\"command\":125,\"bits\":12,\"freq\":40000,\"repeat\":3}"
    "},"
    "\"soundbar\":{"
    "\"power\":{\"protocol\":\"nec\",\"address\":46723,\"command\":77,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"mute\":{\"protocol\":\"nec\",\"address\":46723,\"command\":67,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"pair\":{\"protocol\":\"nec\",\"address\":46723,\"command\":28,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"input\":{\"protocol\":\"nec\",\"address\":46723,\"command\":29,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"vol_up\":{\"protocol\":\"nec\",\"address\":46723,\"command\":11,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"vol_down\":{\"protocol\":\"nec\",\"address\":46723,\"command\":14,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"next\":{\"protocol\":\"nec\",\"address\":46723,\"command\":17,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"prev\":{\"protocol\":\"nec\",\"address\":46723,\"command\":16,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"play\":{\"protocol\":\"nec\",\"address\":46723,\"command\":13,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"music\":{\"protocol\":\"nec\",\"address\":46723,\"command\":69,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"news\":{\"protocol\":\"nec\",\"address\":46723,\"command\":78,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"movie\":{\"protocol\":\"nec\",\"address\":46723,\"command\":71,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"treb_up\":{\"protocol\":\"nec\",\"address\":46723,\"command\":26,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"treb_down\":{\"protocol\":\"nec\",\"address\":46723,\"command\":66,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"bass_up\":{\"protocol\":\"nec\",\"address\":46723,\"command\":23,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"bass_down\":{\"protocol\":\"nec\",\"address\":46723,\"command\":24,\"bits\":32,\"freq\":38000,\"repeat\":3},"
    "\"eq_3d\":{\"protocol\":\"nec\",\"address\":46723,\"command\":76,\"bits\":32,\"freq\":38000,\"repeat\":3}"
    "}"
    "}"
    "}";

static cJSON *s_root;
static SemaphoreHandle_t s_lock;

static int name_ok(const char *s)
{
    if (!s || !s[0] || strlen(s) > 31) {
        return 0;
    }
    for (const char *p = s; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) {
            return 0;
        }
    }
    return 1;
}

static cJSON *devices_obj(void)
{
    if (!s_root) {
        return NULL;
    }
    cJSON *dev = cJSON_GetObjectItemCaseSensitive(s_root, "devices");
    if (!cJSON_IsObject(dev)) {
        cJSON_DeleteItemFromObject(s_root, "devices");
        dev = cJSON_CreateObject();
        cJSON_AddItemToObject(s_root, "devices", dev);
    }
    return dev;
}

static esp_err_t persist_unlocked(void)
{
    char *printed = cJSON_Print(s_root);
    if (!printed) {
        return ESP_ERR_NO_MEM;
    }
    FILE *f = fopen(CATALOG_PATH, "w");
    if (!f) {
        free(printed);
        ESP_LOGE(TAG, "open %s for write failed", CATALOG_PATH);
        return ESP_FAIL;
    }
    fputs(printed, f);
    fclose(f);
    free(printed);
    ESP_LOGI(TAG, "saved %s", CATALOG_PATH);
    return ESP_OK;
}

static esp_err_t load_from_path(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    long sz = ftell(f);
    if (sz <= 0 || sz > 64 * 1024) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = 0;
    cJSON *parsed = cJSON_Parse(buf);
    free(buf);
    if (!parsed || !cJSON_IsObject(parsed)) {
        cJSON_Delete(parsed);
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON_Delete(s_root);
    s_root = parsed;
    return ESP_OK;
}

esp_err_t catalog_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 4,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed (%s) — using compiled-in codes only",
                 esp_err_to_name(err));
    } else {
        size_t total = 0, used = 0;
        if (esp_spiffs_info("storage", &total, &used) == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS total=%u used=%u", (unsigned)total, (unsigned)used);
        }
        err = load_from_path(CATALOG_PATH);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "loaded %s", CATALOG_PATH);
            return ESP_OK;
        }
        ESP_LOGI(TAG, "no %s (%s) — compiled-in defaults", CATALOG_PATH, esp_err_to_name(err));
    }

    s_root = cJSON_Parse(k_defaults);
    if (!s_root) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t catalog_lookup(const char *device, const char *command, cJSON **out)
{
    if (!out || !name_ok(device) || !name_ok(command)) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON *devs = devices_obj();
    cJSON *dev = cJSON_GetObjectItemCaseSensitive(devs, device);
    if (!cJSON_IsObject(dev)) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    cJSON *cmd = cJSON_GetObjectItemCaseSensitive(dev, command);
    if (!cJSON_IsObject(cmd)) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    *out = cmd;
    return ESP_OK;
}

void catalog_unlock(void)
{
    xSemaphoreGive(s_lock);
}

esp_err_t catalog_put(const char *device, const char *command, const cJSON *obj)
{
    if (!name_ok(device) || !name_ok(command) || !cJSON_IsObject(obj)) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *copy = cJSON_Duplicate(obj, true);
    if (!copy) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON *devs = devices_obj();
    cJSON *dev = cJSON_GetObjectItemCaseSensitive(devs, device);
    if (!cJSON_IsObject(dev)) {
        dev = cJSON_CreateObject();
        cJSON_AddItemToObject(devs, device, dev);
    }
    cJSON_DeleteItemFromObject(dev, command);
    cJSON_AddItemToObject(dev, command, copy);
    esp_err_t err = persist_unlocked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t catalog_delete(const char *device, const char *command)
{
    if (!name_ok(device) || !name_ok(command)) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON *devs = devices_obj();
    cJSON *dev = cJSON_GetObjectItemCaseSensitive(devs, device);
    if (!cJSON_IsObject(dev) || !cJSON_GetObjectItemCaseSensitive(dev, command)) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    cJSON_DeleteItemFromObject(dev, command);
    if (cJSON_GetArraySize(dev) == 0) {
        cJSON_DeleteItemFromObject(devs, device);
    }
    esp_err_t err = persist_unlocked();
    xSemaphoreGive(s_lock);
    return err;
}

char *catalog_list_json(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    char *printed = cJSON_PrintUnformatted(s_root);
    xSemaphoreGive(s_lock);
    return printed;
}
