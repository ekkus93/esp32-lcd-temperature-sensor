#include "device_settings_service.hpp"
#include "nvs.h"
namespace monitor {
esp_err_t DeviceSettingsService::initialize(bool simulated) {
    namespace_ = simulated ? "device_sim" : "monitor_device";
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_)
        return ESP_ERR_NO_MEM;
    nvs_handle_t handle;
    auto result = nvs_open(namespace_, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        writable_ = true;
        return ESP_OK;
    }
    if (result != ESP_OK)
        return result;
    size_t size = 0;
    result = nvs_get_blob(handle, "options1", nullptr, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        writable_ = true;
        return ESP_OK;
    }
    if (result != ESP_OK || size > 512) {
        nvs_close(handle);
        return ESP_ERR_INVALID_SIZE;
    }
    std::string bytes(size, '\0');
    result = nvs_get_blob(handle, "options1", bytes.data(), &size);
    nvs_close(handle);
    if (result != ESP_OK)
        return result;
    const auto decoded = decode_device_options(bytes);
    if (!decoded)
        return ESP_ERR_INVALID_CRC;
    options_ = *decoded;
    writable_ = true;
    return ESP_OK;
}
bool DeviceSettingsService::get(DeviceOptions &options, uint64_t &revision, bool &writable) {
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE)
        return false;
    options = options_;
    revision = revision_;
    writable = writable_;
    xSemaphoreGive(mutex_);
    return true;
}
esp_err_t DeviceSettingsService::save(const DeviceOptions &options, uint64_t expected_revision) {
    auto bytes = encode_device_options(options);
    if (bytes.empty())
        return ESP_ERR_INVALID_ARG;
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (writable_ && expected_revision == revision_) {
        if (bytes == encode_device_options(options_))
            result = ESP_OK;
        else {
            nvs_handle_t handle;
            result = nvs_open(namespace_, NVS_READWRITE, &handle);
            if (result == ESP_OK) {
                result = nvs_set_blob(handle, "options1", bytes.data(), bytes.size());
                if (result == ESP_OK)
                    result = nvs_commit(handle);
                nvs_close(handle);
            }
            if (result == ESP_OK) {
                options_ = options;
                ++revision_;
            }
        }
    }
    xSemaphoreGive(mutex_);
    return result;
}
} // namespace monitor
