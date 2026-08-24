#pragma once

#include "cJSON.h"
#include "esp_err.h"

esp_err_t catalog_init(void);

/* Lookup a named device/command. Caller must catalog_unlock() after using *out. */
esp_err_t catalog_lookup(const char *device, const char *command, cJSON **out);
void catalog_unlock(void);

/* Copy obj into the catalog (device/command created as needed) and persist. */
esp_err_t catalog_put(const char *device, const char *command, const cJSON *obj);
esp_err_t catalog_delete(const char *device, const char *command);

/* malloc'd JSON string of the whole catalog. Caller frees. */
char *catalog_list_json(void);
