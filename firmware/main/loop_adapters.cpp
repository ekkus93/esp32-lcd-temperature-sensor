// Device implementations of the main loop's ports (God file refactor G15).
#include "loop_adapters.hpp"
#include "clock_service.hpp"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "ota_service.hpp"
#include "provisioning_service.hpp"
#include <utility>
namespace monitor {
int64_t DeviceLoopClock::now_ms() {
    return esp_timer_get_time() / 1000;
}
std::optional<int64_t> DeviceLoopClock::trusted_utc() {
    return monitor::trusted_utc();
}
bool DeviceLoopClock::synchronized() {
    return clock_synchronized();
}
std::string DeviceLoopClock::utc_text() {
    return monitor::utc_text();
}
std::string DeviceLoopClock::local_time_text() {
    return monitor::local_time_text();
}
bool DisplayLoopAdapter::hand_over(DisplayFrame &&frame) {
    if (!task_started_) {
        display_.update(frame.registry, *frame.trend_cache, frame.options, frame.status,
                        frame.uptime_ms, frame.provisioning, frame.ssid, frame.wifi_key,
                        frame.local_name);
        return true;
    }
    // Swap in under the lock, so the display task waits only for the swap; the frame it replaces
    // is freed after the lock is released (when `frame` goes out of scope).
    if (xSemaphoreTake(context_.mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;
    std::swap(context_.frame, frame);
    xSemaphoreGive(context_.mutex);
    return true;
}
DeviceOptions DisplayLoopAdapter::options() {
    DeviceOptions options;
    uint64_t revision = 0;
    bool writable = false;
    settings_.get(options, revision, writable);
    return options;
}
unsigned DisplayLoopAdapter::button_progress() {
    return boot_hold_progress();
}
std::string DisplayLoopAdapter::ota_state() {
    return ota_status().state;
}
bool TrendRestoreLoopAdapter::start() {
    restore_.started = true;
    return xTaskCreate(trend_restore_task, "trend_restore", 8192, &restore_, 1, nullptr) == pdPASS;
}
std::optional<TrendPoints> TrendRestoreLoopAdapter::take() {
    std::optional<TrendPoints> stored;
    if (xSemaphoreTake(restore_.mutex, 0) == pdTRUE) {
        if (restore_.done)
            stored = std::move(restore_.stored);
        xSemaphoreGive(restore_.mutex);
    }
    return stored;
}
bool OtaLoopAdapter::pending_verify() {
    const auto *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state{};
    const auto state_result =
        running ? esp_ota_get_state_partition(running, &state) : ESP_ERR_NOT_FOUND;
    return state_result == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
}
void OtaLoopAdapter::roll_back() {
    esp_ota_mark_app_invalid_rollback_and_reboot();
}
esp_err_t OtaLoopAdapter::mark_valid() {
    return esp_ota_mark_app_valid_cancel_rollback();
}
} // namespace monitor
