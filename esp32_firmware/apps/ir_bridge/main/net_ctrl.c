#include "net_ctrl.h"

#include "app.h"
#include "catalog.h"
#include "ir_tx.h"
#include "ir_rx.h"

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>

static const char *TAG = "net_ctrl";

#define NVS_NS_MQTT      "mqtt"
#define NVS_KEY_URI      "uri"
#define NVS_KEY_USER     "username"
#define NVS_KEY_PASS     "password"
#define NVS_KEY_PREFIX   "prefix"
#define BODY_MAX         8192
#define PREFIX_MAX       32

static httpd_handle_t s_httpd;
static esp_mqtt_client_handle_t s_mqtt;
static SemaphoreHandle_t s_mqtt_lock;
static char s_mqtt_uri[128];
static char s_mqtt_user[64];
static char s_mqtt_pass[64];
static char s_prefix[PREFIX_MAX] = "ir";
static int s_mqtt_connected;

static const char k_index_html[] =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
#if IR_TX_ENABLE && IR_RX_ENABLE
    "<title>IR-MC</title>"
#elif IR_TX_ENABLE
    "<title>IR-MC blast</title>"
#else
    "<title>IR-MC listen</title>"
#endif
    "<style>"
    "body{font-family:system-ui,sans-serif;max-width:42rem;margin:1.5rem auto;padding:0 1rem}"
    "button{margin:.2rem;padding:.4rem .7rem}"
    "pre{background:#f4f4f4;padding:.5rem;overflow:auto}"
    "textarea{width:100%;font-family:monospace}"
    "section{margin:1.5rem 0;padding:1rem 0;border-top:1px solid #ddd}"
    "</style>"
#if IR_TX_ENABLE && IR_RX_ENABLE
    "<h1>IR-MC</h1>"
#elif IR_TX_ENABLE
    "<h1>IR-MC blast</h1><p>Transmit only (GPIO18). Receive is disabled on this module.</p>"
#else
    "<h1>IR-MC listen</h1><p>Receive only (GPIO19). Transmit is disabled on this module.</p>"
#endif
    "<p id=st>loading…</p>"
    "<section><h2>Named commands</h2><div id=cmds></div></section>"
#if IR_RX_ENABLE
    "<section><h2>Learn from remote</h2>"
    "<p>Point the remote at the TSOP on GPIO19 and press one button.</p>"
    "<p>device <input id=ld value=sceptre size=10> command <input id=lc value=power size=10>"
    "<button onclick=\"learn()\">Learn (8s)</button></p></section>"
#endif
#if IR_TX_ENABLE
    "<section><h2>Send raw</h2>"
    "<textarea id=raw rows=6>{ \"freq\": 38000, \"timings\": [9000,4500,560,1690,560,560], \"repeat\": 1 }</textarea>"
    "<p><button onclick=\"sendRaw()\">Send raw</button></p></section>"
#endif
    "<pre id=log></pre>"
    "<script>"
    "async function st(){const r=await fetch('/status');const j=await r.json();"
    "document.getElementById('st').textContent=j.identity+' · '+j.ip+' · v'+j.version+"
    "' · mqtt '+(j.mqtt&&j.mqtt.connected?'up':'down');}"
    "async function load(){const r=await fetch('/devices');const j=await r.json();"
    "const el=document.getElementById('cmds');el.innerHTML='';"
    "const devs=j.devices||{};"
    "for(const d of Object.keys(devs)){const h=document.createElement('h3');h.textContent=d;el.appendChild(h);"
    "for(const c of Object.keys(devs[d])){const b=document.createElement('button');b.textContent=c;"
    "b.onclick=()=>post('/ir/'+d+'/'+c);el.appendChild(b);}}}"
    "async function post(url,body){const r=await fetch(url,{method:'POST',"
    "headers:{'content-type':'application/json'},body:body?JSON.stringify(body):'{}'});"
    "document.getElementById('log').textContent=await r.text();}"
    "function sendRaw(){post('/ir/send',JSON.parse(document.getElementById('raw').value));}"
    "async function learn(){document.getElementById('log').textContent='listening… press the remote';"
    "const body={timeout_ms:8000,device:document.getElementById('ld').value,"
    "command:document.getElementById('lc').value};"
    "const r=await fetch('/ir/learn',{method:'POST',headers:{'content-type':'application/json'},"
    "body:JSON.stringify(body)}); document.getElementById('log').textContent=await r.text(); load();}"
    "st();load();"
    "</script>";

static int64_t uptime_s(void)
{
    return esp_timer_get_time() / 1000000LL;
}

static void publish_status(int retain)
{
    if (!s_mqtt || !s_mqtt_connected) {
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "online", true);
    cJSON_AddStringToObject(root, "role", ROLE);
    cJSON_AddStringToObject(root, "identity", g_identity);
    cJSON_AddStringToObject(root, "ip", g_ip_str[0] ? g_ip_str : "");
    cJSON_AddStringToObject(root, "app", APP_NAME);
    cJSON_AddStringToObject(root, "version", APP_VERSION);
    cJSON_AddNumberToObject(root, "uptime_s", (double)uptime_s());
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return;
    }
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/status", s_prefix);
    esp_mqtt_client_publish(s_mqtt, topic, payload, 0, 0, retain);
    free(payload);
}

static void publish_ack(const char *device, const char *command, esp_err_t err, int64_t ms)
{
    if (!s_mqtt || !s_mqtt_connected) {
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", err == ESP_OK);
    if (device) {
        cJSON_AddStringToObject(root, "device", device);
    }
    if (command) {
        cJSON_AddStringToObject(root, "command", command);
    }
    cJSON_AddStringToObject(root, "error", err == ESP_OK ? "" : esp_err_to_name(err));
    cJSON_AddNumberToObject(root, "ms", (double)ms);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return;
    }
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/ack", s_prefix);
    esp_mqtt_client_publish(s_mqtt, topic, payload, 0, 0, 0);
    free(payload);
}

static esp_err_t execute_named(const char *device, const char *command, int repeat_override, int64_t *ms_out)
{
    cJSON *cmd = NULL;
    esp_err_t err = catalog_lookup(device, command, &cmd);
    if (err != ESP_OK) {
        return err;
    }
    int64_t t0 = esp_timer_get_time();
    err = ir_execute_json(cmd, repeat_override);
    catalog_unlock();
    if (ms_out) {
        *ms_out = (esp_timer_get_time() - t0) / 1000;
    }
    return err;
}

static esp_err_t execute_payload(cJSON *root, const char *fallback_device, const char *fallback_command,
                                 char *dev_out, size_t dev_len, char *cmd_out, size_t cmd_len,
                                 int64_t *ms_out)
{
    const char *device = fallback_device;
    const char *command = fallback_command;
    int repeat_override = -1;

    if (cJSON_IsObject(root)) {
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "device");
        const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "command");
        const cJSON *r = cJSON_GetObjectItemCaseSensitive(root, "repeat");
        if (cJSON_IsString(d) && d->valuestring) {
            device = d->valuestring;
        }
        if (cJSON_IsString(c) && c->valuestring) {
            command = c->valuestring;
        }
        if (cJSON_IsNumber(r)) {
            repeat_override = r->valueint;
        }
    }

    if (dev_out && dev_len) {
        snprintf(dev_out, dev_len, "%s", device ? device : "");
    }
    if (cmd_out && cmd_len) {
        snprintf(cmd_out, cmd_len, "%s", command ? command : "");
    }

    int64_t t0 = esp_timer_get_time();
    esp_err_t err;
    if (device && device[0] && command && command[0] &&
        (!root || (!cJSON_GetObjectItemCaseSensitive(root, "protocol") &&
                   !cJSON_GetObjectItemCaseSensitive(root, "timings")))) {
        err = execute_named(device, command, repeat_override, NULL);
    } else if (cJSON_IsObject(root)) {
        err = ir_execute_json(root, repeat_override);
    } else {
        err = ESP_ERR_INVALID_ARG;
    }
    if (ms_out) {
        *ms_out = (esp_timer_get_time() - t0) / 1000;
    }
    return err;
}

static esp_err_t json_result(httpd_req_t *req, esp_err_t err, const char *device, const char *command, int64_t ms)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", err == ESP_OK);
    if (device && device[0]) {
        cJSON_AddStringToObject(root, "device", device);
    }
    if (command && command[0]) {
        cJSON_AddStringToObject(root, "command", command);
    }
    cJSON_AddNumberToObject(root, "ms", (double)ms);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(root, "error", esp_err_to_name(err));
    }
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        httpd_resp_set_status(req, err == ESP_ERR_NOT_FOUND ? "404 Not Found" : "400 Bad Request");
    }
    esp_err_t send_err = httpd_resp_sendstr(req, payload);
    free(payload);
    return send_err;
}

static char *read_body_alloc(httpd_req_t *req)
{
    int remaining = req->content_len;
    if (remaining < 0) {
        remaining = 0;
    }
    if (remaining >= BODY_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return NULL;
    }
    char *buf = malloc((size_t)remaining + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return NULL;
    }
    int total = 0;
    while (total < remaining) {
        int n = httpd_req_recv(req, buf + total, remaining - total);
        if (n <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return NULL;
        }
        total += n;
    }
    buf[total] = 0;
    return buf;
}

static cJSON *status_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "role", ROLE);
    cJSON_AddStringToObject(root, "identity", g_identity);
    cJSON_AddStringToObject(root, "ip", g_ip_str[0] ? g_ip_str : "");
    cJSON_AddStringToObject(root, "app", APP_NAME);
    cJSON_AddStringToObject(root, "version", APP_VERSION);
    cJSON_AddNumberToObject(root, "uptime_s", (double)uptime_s());
    cJSON_AddNumberToObject(root, "http_port", HTTP_PORT);
    cJSON_AddStringToObject(root, "prefix", s_prefix);
    cJSON_AddBoolToObject(root, "tx", IR_TX_ENABLE);
    cJSON_AddBoolToObject(root, "rx", IR_RX_ENABLE);

    cJSON *mqtt = cJSON_CreateObject();
    cJSON_AddBoolToObject(mqtt, "configured", s_mqtt_uri[0] != 0);
    cJSON_AddBoolToObject(mqtt, "connected", s_mqtt_connected);
    cJSON_AddStringToObject(mqtt, "uri", s_mqtt_uri[0] ? s_mqtt_uri : "");
    cJSON_AddItemToObject(root, "mqtt", mqtt);
    return root;
}

static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, k_index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_status(httpd_req_t *req)
{
    cJSON *root = status_json();
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static esp_err_t handler_health(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "ok\n");
}

static esp_err_t handler_devices(httpd_req_t *req)
{
    char *json = catalog_list_json();
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static void split_device_command(const char *path, char *device, size_t dlen, char *command, size_t clen)
{
    device[0] = 0;
    command[0] = 0;
    const char *slash = strchr(path, '/');
    if (!slash || slash == path || slash[1] == 0) {
        return;
    }
    size_t n = (size_t)(slash - path);
    if (n >= dlen) {
        n = dlen - 1;
    }
    memcpy(device, path, n);
    device[n] = 0;
    snprintf(command, clen, "%s", slash + 1);
    char *q = strchr(command, '?');
    if (q) {
        *q = 0;
    }
}

static cJSON *frame_to_json(const ir_frame_t *fr)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "protocol", fr->protocol);
    cJSON_AddNumberToObject(root, "address", (double)fr->address);
    cJSON_AddNumberToObject(root, "command", (double)fr->command);
    cJSON_AddNumberToObject(root, "bits", fr->bits);
    cJSON_AddNumberToObject(root, "freq", (double)fr->freq_hz);
    cJSON *tim = cJSON_CreateArray();
    for (size_t i = 0; i < fr->n_timings; i++) {
        cJSON_AddItemToArray(tim, cJSON_CreateNumber(fr->timings[i]));
    }
    cJSON_AddItemToObject(root, "timings", tim);
    return root;
}

static esp_err_t handler_learn(httpd_req_t *req)
{
#if !IR_RX_ENABLE
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "501 Not Implemented");
    return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"this module is transmit-only\"}");
#else
    char *body = read_body_alloc(req);
    if (!body) {
        return ESP_FAIL;
    }
    cJSON *parsed = body[0] ? cJSON_Parse(body) : NULL;
    free(body);

    int timeout_ms = 8000;
    const char *device = NULL;
    const char *command = NULL;
    if (cJSON_IsObject(parsed)) {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(parsed, "timeout_ms");
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(parsed, "device");
        const cJSON *c = cJSON_GetObjectItemCaseSensitive(parsed, "command");
        if (cJSON_IsNumber(t) && t->valueint > 0) {
            timeout_ms = t->valueint;
        }
        if (cJSON_IsString(d) && d->valuestring && d->valuestring[0]) {
            device = d->valuestring;
        }
        if (cJSON_IsString(c) && c->valuestring && c->valuestring[0]) {
            command = c->valuestring;
        }
    }

    ir_frame_t fr;
    esp_err_t err = ir_rx_learn(timeout_ms, &fr);
    if (err != ESP_OK) {
        cJSON_Delete(parsed);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "408 Request Timeout");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"timeout — no IR received\"}");
    }

    cJSON *code = frame_to_json(&fr);
    bool saved = false;
    if (device && command) {
        if (catalog_put(device, command, code) == ESP_OK) {
            saved = true;
            cJSON_AddStringToObject(code, "device", device);
            cJSON_AddStringToObject(code, "command", command);
        }
    }
    cJSON_Delete(parsed);
    cJSON_AddBoolToObject(code, "ok", true);
    cJSON_AddBoolToObject(code, "saved", saved);
    char *json = cJSON_PrintUnformatted(code);
    cJSON_Delete(code);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
#endif
}

static esp_err_t handler_ir(httpd_req_t *req)
{
#if !IR_TX_ENABLE
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "501 Not Implemented");
    return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"this module is receive-only\"}");
#else
    char *body = read_body_alloc(req);
    if (!body) {
        return ESP_FAIL;
    }

    cJSON *parsed = body[0] ? cJSON_Parse(body) : NULL;
    free(body);
    char device[32] = {0};
    char command[32] = {0};

    const char *uri = req->uri;
    if (strncmp(uri, "/ir/", 4) == 0) {
        const char *rest = uri + 4;
        if (strcmp(rest, "send") != 0 && strncmp(rest, "send?", 5) != 0) {
            split_device_command(rest, device, sizeof(device), command, sizeof(command));
        }
    }

    int64_t ms = 0;
    esp_err_t err = execute_payload(parsed, device[0] ? device : NULL, command[0] ? command : NULL,
                                    device, sizeof(device), command, sizeof(command), &ms);
    cJSON_Delete(parsed);
    publish_ack(device, command, err, ms);
    return json_result(req, err, device, command, ms);
#endif
}

static esp_err_t handler_put_command(httpd_req_t *req)
{
    char *body = read_body_alloc(req);
    if (!body) {
        return ESP_FAIL;
    }
    cJSON *parsed = cJSON_Parse(body);
    free(body);
    if (!cJSON_IsObject(parsed)) {
        cJSON_Delete(parsed);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json object required");
        return ESP_FAIL;
    }
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(parsed, "device");
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(parsed, "command");
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(parsed, "code");
    if (!cJSON_IsString(d) || !cJSON_IsString(c)) {
        cJSON_Delete(parsed);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "device and command strings required");
        return ESP_FAIL;
    }
    char device[32], command[32];
    snprintf(device, sizeof(device), "%s", d->valuestring);
    snprintf(command, sizeof(command), "%s", c->valuestring);
    const cJSON *obj = cJSON_IsObject(code) ? code : parsed;
    esp_err_t err = catalog_put(device, command, obj);
    cJSON_Delete(parsed);
    return json_result(req, err, device, command, 0);
}

static esp_err_t handler_delete_command(httpd_req_t *req)
{
    char *body = read_body_alloc(req);
    if (!body) {
        return ESP_FAIL;
    }
    cJSON *parsed = cJSON_Parse(body);
    free(body);
    const cJSON *d = cJSON_IsObject(parsed) ? cJSON_GetObjectItemCaseSensitive(parsed, "device") : NULL;
    const cJSON *c = cJSON_IsObject(parsed) ? cJSON_GetObjectItemCaseSensitive(parsed, "command") : NULL;
    if (!cJSON_IsString(d) || !cJSON_IsString(c)) {
        cJSON_Delete(parsed);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "device and command strings required");
        return ESP_FAIL;
    }
    esp_err_t err = catalog_delete(d->valuestring, c->valuestring);
    char dev[32], cmd[32];
    snprintf(dev, sizeof(dev), "%s", d->valuestring);
    snprintf(cmd, sizeof(cmd), "%s", c->valuestring);
    cJSON_Delete(parsed);
    return json_result(req, err, dev, cmd, 0);
}

static esp_err_t nvs_save_mqtt(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_MQTT, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    nvs_set_str(h, NVS_KEY_URI, s_mqtt_uri);
    nvs_set_str(h, NVS_KEY_USER, s_mqtt_user);
    nvs_set_str(h, NVS_KEY_PASS, s_mqtt_pass);
    nvs_set_str(h, NVS_KEY_PREFIX, s_prefix);
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static void mqtt_start_locked(void);

static esp_err_t handler_config_mqtt(httpd_req_t *req)
{
    char *body = read_body_alloc(req);
    if (!body) {
        return ESP_FAIL;
    }
    cJSON *parsed = cJSON_Parse(body);
    free(body);
    if (!cJSON_IsObject(parsed)) {
        cJSON_Delete(parsed);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json object required");
        return ESP_FAIL;
    }
    const cJSON *uri = cJSON_GetObjectItemCaseSensitive(parsed, "uri");
    const cJSON *user = cJSON_GetObjectItemCaseSensitive(parsed, "username");
    const cJSON *pass = cJSON_GetObjectItemCaseSensitive(parsed, "password");
    const cJSON *prefix = cJSON_GetObjectItemCaseSensitive(parsed, "prefix");
    if (cJSON_IsString(uri) && uri->valuestring) {
        snprintf(s_mqtt_uri, sizeof(s_mqtt_uri), "%s", uri->valuestring);
    }
    if (cJSON_IsString(user) && user->valuestring) {
        snprintf(s_mqtt_user, sizeof(s_mqtt_user), "%s", user->valuestring);
    }
    if (cJSON_IsString(pass) && pass->valuestring) {
        snprintf(s_mqtt_pass, sizeof(s_mqtt_pass), "%s", pass->valuestring);
    }
    if (cJSON_IsString(prefix) && prefix->valuestring && prefix->valuestring[0]) {
        snprintf(s_prefix, sizeof(s_prefix), "%s", prefix->valuestring);
    }
    cJSON_Delete(parsed);

    esp_err_t err = nvs_save_mqtt();
    if (err != ESP_OK) {
        return json_result(req, err, NULL, NULL, 0);
    }
    mqtt_start_locked();
    return json_result(req, ESP_OK, NULL, NULL, 0);
}

static bool topic_is_self(const char *topic)
{
    char status[64], ack[64];
    snprintf(status, sizeof(status), "%s/status", s_prefix);
    snprintf(ack, sizeof(ack), "%s/ack", s_prefix);
    return strcmp(topic, status) == 0 || strcmp(topic, ack) == 0;
}

static void handle_mqtt_message(const char *topic, int topic_len, const char *data, int data_len)
{
    char tbuf[128];
    if (topic_len <= 0 || topic_len >= (int)sizeof(tbuf)) {
        return;
    }
    memcpy(tbuf, topic, (size_t)topic_len);
    tbuf[topic_len] = 0;
    if (topic_is_self(tbuf)) {
        return;
    }

    size_t plen = strlen(s_prefix);
    if (strncmp(tbuf, s_prefix, plen) != 0 || tbuf[plen] != '/') {
        return;
    }
    const char *rest = tbuf + plen + 1;

    char body[BODY_MAX];
    int ncopy = data_len;
    if (ncopy >= (int)sizeof(body)) {
        ncopy = (int)sizeof(body) - 1;
    }
    if (ncopy < 0) {
        ncopy = 0;
    }
    memcpy(body, data ? data : "", (size_t)ncopy);
    body[ncopy] = 0;

    cJSON *parsed = body[0] ? cJSON_Parse(body) : NULL;
    char device[32] = {0};
    char command[32] = {0};
    const char *fb_dev = NULL;
    const char *fb_cmd = NULL;

    if (strcmp(rest, "raw") == 0 || strcmp(rest, "send") == 0) {
        /* payload JSON only */
    } else {
        split_device_command(rest, device, sizeof(device), command, sizeof(command));
        fb_dev = device[0] ? device : NULL;
        fb_cmd = command[0] ? command : NULL;
    }

    int64_t ms = 0;
    esp_err_t err = execute_payload(parsed, fb_dev, fb_cmd, device, sizeof(device), command, sizeof(command), &ms);
    cJSON_Delete(parsed);
    publish_ack(device, command, err, ms);
    ESP_LOGI(TAG, "mqtt %s -> %s (%lld ms)", tbuf, esp_err_to_name(err), (long long)ms);
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;
    switch (id) {
    case MQTT_EVENT_CONNECTED: {
        s_mqtt_connected = 1;
        char filter[64];
        snprintf(filter, sizeof(filter), "%s/#", s_prefix);
        esp_mqtt_client_subscribe(s_mqtt, filter, 0);
        publish_status(1);
        ESP_LOGI(TAG, "MQTT connected, subscribed %s", filter);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        s_mqtt_connected = 0;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_DATA:
        handle_mqtt_message(ev->topic, ev->topic_len, ev->data, ev->data_len);
        break;
    default:
        break;
    }
}

static void mqtt_stop(void)
{
    if (s_mqtt) {
        esp_mqtt_client_stop(s_mqtt);
        esp_mqtt_client_destroy(s_mqtt);
        s_mqtt = NULL;
    }
    s_mqtt_connected = 0;
}

static void mqtt_start_locked(void)
{
    if (!s_mqtt_lock) {
        s_mqtt_lock = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_mqtt_lock, portMAX_DELAY);
    mqtt_stop();
    if (!s_mqtt_uri[0]) {
        ESP_LOGI(TAG, "MQTT not configured (set via NVS or POST /config/mqtt)");
        xSemaphoreGive(s_mqtt_lock);
        return;
    }

    static char lwt_topic[64];
    snprintf(lwt_topic, sizeof(lwt_topic), "%s/status", s_prefix);
    static const char lwt_msg[] = "{\"online\":false}";

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = s_mqtt_uri,
        .credentials.username = s_mqtt_user[0] ? s_mqtt_user : NULL,
        .credentials.authentication.password = s_mqtt_pass[0] ? s_mqtt_pass : NULL,
        .session.last_will.topic = lwt_topic,
        .session.last_will.msg = lwt_msg,
        .session.last_will.retain = true,
        .session.last_will.qos = 0,
        .buffer.size = 4096,
    };
    s_mqtt = esp_mqtt_client_init(&cfg);
    if (!s_mqtt) {
        ESP_LOGE(TAG, "mqtt init failed");
        xSemaphoreGive(s_mqtt_lock);
        return;
    }
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    esp_err_t err = esp_mqtt_client_start(s_mqtt);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mqtt start: %s", esp_err_to_name(err));
        mqtt_stop();
    }
    xSemaphoreGive(s_mqtt_lock);
}

static void nvs_load_mqtt(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_MQTT, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_mqtt_uri);
    if (nvs_get_str(h, NVS_KEY_URI, s_mqtt_uri, &len) != ESP_OK) {
        s_mqtt_uri[0] = 0;
    }
    len = sizeof(s_mqtt_user);
    if (nvs_get_str(h, NVS_KEY_USER, s_mqtt_user, &len) != ESP_OK) {
        s_mqtt_user[0] = 0;
    }
    len = sizeof(s_mqtt_pass);
    if (nvs_get_str(h, NVS_KEY_PASS, s_mqtt_pass, &len) != ESP_OK) {
        s_mqtt_pass[0] = 0;
    }
    len = sizeof(s_prefix);
    if (nvs_get_str(h, NVS_KEY_PREFIX, s_prefix, &len) != ESP_OK || !s_prefix[0]) {
        snprintf(s_prefix, sizeof(s_prefix), "ir");
    }
    nvs_close(h);
}

static esp_err_t start_http(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = HTTP_PORT;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 20;
    cfg.stack_size = 8192;
    cfg.recv_wait_timeout = 15;
    cfg.send_wait_timeout = 15;

    esp_err_t err = httpd_start(&s_httpd, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = handler_root },
        { .uri = "/status", .method = HTTP_GET, .handler = handler_status },
        { .uri = "/health", .method = HTTP_GET, .handler = handler_health },
        { .uri = "/devices", .method = HTTP_GET, .handler = handler_devices },
        { .uri = "/commands", .method = HTTP_GET, .handler = handler_devices },
        { .uri = "/commands", .method = HTTP_PUT, .handler = handler_put_command },
        { .uri = "/commands", .method = HTTP_DELETE, .handler = handler_delete_command },
        { .uri = "/config/mqtt", .method = HTTP_POST, .handler = handler_config_mqtt },
        { .uri = "/ir/learn", .method = HTTP_POST, .handler = handler_learn },
        { .uri = "/ir/*", .method = HTTP_POST, .handler = handler_ir },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_httpd, &uris[i]);
    }
    ESP_LOGI(TAG, "HTTP on :%d", HTTP_PORT);
    return ESP_OK;
}

esp_err_t net_ctrl_start(void)
{
    if (!s_mqtt_lock) {
        s_mqtt_lock = xSemaphoreCreateMutex();
        if (!s_mqtt_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    nvs_load_mqtt();
    esp_err_t err = start_http();
    if (err != ESP_OK) {
        return err;
    }
    if (g_ip_str[0]) {
        mqtt_start_locked();
    }
    return ESP_OK;
}

void net_ctrl_on_got_ip(void)
{
    mqtt_start_locked();
}

void net_ctrl_on_lost_ip(void)
{
    if (s_mqtt_lock) {
        xSemaphoreTake(s_mqtt_lock, portMAX_DELAY);
        mqtt_stop();
        xSemaphoreGive(s_mqtt_lock);
    } else {
        mqtt_stop();
    }
}
