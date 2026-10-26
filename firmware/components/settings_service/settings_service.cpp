#include "settings_service.hpp"
#include "nvs.h"
#include "settings_codec.hpp"
namespace monitor {
esp_err_t SettingsService::load(Registry &registry) {
    nvs_handle_t handle;
    auto result = nvs_open(namespace_, NVS_READONLY, &handle);
    if (result != ESP_OK)
        return result;
    size_t size = 0;
    result = nvs_get_blob(handle, "sensors", nullptr, &size);
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
    return ESP_OK;
}
esp_err_t SettingsService::save(const Registry &registry) {
    auto bytes = encode_settings(registry.sensors());
    if (bytes.empty())
        return ESP_ERR_INVALID_ARG;
    if (bytes == previous_)
        return ESP_OK;
    nvs_handle_t handle;
    auto result = nvs_open(namespace_, NVS_READWRITE, &handle);
    if (result != ESP_OK)
        return result;
    result = nvs_set_blob(handle, "sensors", bytes.data(), bytes.size());
    if (result == ESP_OK)
        result = nvs_commit(handle);
    nvs_close(handle);
    if (result == ESP_OK)
        previous_ = bytes;
    return result;
}
} // namespace monitor
