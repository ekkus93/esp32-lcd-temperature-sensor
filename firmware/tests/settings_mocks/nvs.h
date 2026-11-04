#pragma once
#include "esp_err.h"
#include <cstddef>
#include <cstdint>
using nvs_handle_t = uint32_t;
constexpr int NVS_READONLY = 0;
constexpr int NVS_READWRITE = 1;
extern "C" {
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
}
