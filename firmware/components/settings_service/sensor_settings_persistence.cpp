#include "sensor_settings_persistence.hpp"
#include <algorithm>
namespace monitor {
esp_err_t SensorSettingsPersistence::write(const Registry &registry, int64_t now_ms) {
    const auto result = service_.save(registry);
    if (result == ESP_OK) {
        // The whole registry was written, automatic changes included.
        unsaved_ = false;
        backoff_ms_ = 0;
        retry_at_ms_ = INT64_MIN;
    } else if (service_.writable()) {
        backoff_ms_ = backoff_ms_ ? std::min(backoff_ms_ * 2, max_backoff_ms) : first_backoff_ms;
        retry_at_ms_ = now_ms + backoff_ms_;
    }
    return result;
}
esp_err_t SensorSettingsPersistence::commit_edits(Registry &registry, const Registry &committed,
                                                  int64_t now_ms) {
    // The owner asked for this write, so it is attempted even during a backoff (the API refuses new
    // edits then, but one may have been queued just before the failure).
    const auto result = write(registry, now_ms);
    if (result != ESP_OK)
        registry = committed;
    return result;
}
esp_err_t SensorSettingsPersistence::persist(const Registry &registry, bool changed,
                                             int64_t now_ms) {
    unsaved_ = unsaved_ || changed;
    if (!unsaved_)
        return ESP_OK;
    if (!service_.writable())
        return ESP_ERR_INVALID_STATE; // damaged stored settings are never overwritten
    if (now_ms < retry_at_ms_)
        return ESP_ERR_NOT_FINISHED;
    return write(registry, now_ms);
}
} // namespace monitor
