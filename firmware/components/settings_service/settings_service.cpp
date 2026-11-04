#include "settings_service.hpp"
#include "nvs.h"
#include "settings_codec.hpp"
namespace monitor {
esp_err_t SettingsService::load(Registry &registry) {
    writable_ = false;
    nvs_handle_t handle;
    auto result = nvs_open(namespace_, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        writable_ = true;
        return result;
    }
    if (result != ESP_OK)
        return result;
    size_t size = 0;
    result = nvs_get_blob(handle, "sensors", nullptr, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        writable_ = true;
        return result;
    }
    if (result != ESP_OK || size > 4096) {
        nvs_close(handle);
        return result == ESP_OK ? ESP_ERR_INVALID_SIZE : result;
    }
    std::string bytes(size, '\0');
    result = nvs_get_blob(handle, "sensors", bytes.data(), &size);
    nvs_close(handle);
    if (result != ESP_OK)
        return result;
    auto decoded = decode_settings(bytes);
    if (!decoded)
        return ESP_ERR_INVALID_CRC;
    Registry candidate;
    for (const auto &sensor : *decoded) {
        if (!candidate.discover(sensor.rom) || !candidate.configure(sensor.rom, sensor.settings))
            return ESP_ERR_INVALID_STATE;
    }
    registry = candidate;
    previous_ = bytes;
    writable_ = true;
    return ESP_OK;
}
esp_err_t SettingsService::save(const Registry &registry) {
    if (!writable_)
        return ESP_ERR_INVALID_STATE;
    auto bytes = encode_settings(registry.sensors());
    if (bytes.empty())
        return ESP_ERR_INVALID_ARG;
    if (bytes == previous_)
        return ESP_OK;
    nvs_handle_t handle;
    auto result = nvs_open(namespace_, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        writable_ = false;
        return result;
    }
    result = nvs_set_blob(handle, "sensors", bytes.data(), bytes.size());
    if (result == ESP_OK)
        result = nvs_commit(handle);
    nvs_close(handle);
    if (result == ESP_OK)
        previous_ = bytes;
    else
        writable_ = false;
    return result;
}
esp_err_t SettingsService::save_or_restore(Registry &candidate, const Registry &committed) {
    const auto result = save(candidate);
    if (result != ESP_OK)
        candidate = committed;
    return result;
}
} // namespace monitor
