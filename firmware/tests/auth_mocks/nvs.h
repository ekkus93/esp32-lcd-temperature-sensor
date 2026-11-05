#pragma once
// In-memory NVS for host tests. Failures can be injected per operation.
#include "esp_err.h"
#include <cstddef>
#include <cstdint>
#include <string>
using nvs_handle_t = uint32_t;
constexpr int NVS_READONLY = 0;
constexpr int NVS_READWRITE = 1;
struct FakeNvs {
    static void reset();
    static bool has(const char *key);
    static std::string get(const char *key);
    static void put(const char *key, const std::string &value);
    static void erase(const char *key);
    static bool fail_open, fail_commit, fail_set;
};
extern "C" {
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *size);
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
}
