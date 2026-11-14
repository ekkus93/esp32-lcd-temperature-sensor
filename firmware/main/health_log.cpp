// The per-cycle health log (moved from app_main, God file refactor G16).
#include "health_log.hpp"
#include "clock_service.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
namespace monitor {
void log_health(const MonitorLoop &loop, const SamplingService &sampling, HistoryService &history,
                bool simulated) {
    const char *tag = "monitor";
    const auto now = loop.cycle_started_ms();
    const auto simulated_tick = loop.cycle_tick();
    const auto &sensor_diagnostics = loop.diagnostics();
    const auto display_frames_dropped = loop.display_frames_dropped();
    if (!simulated || simulated_tick % 20 == 0) {
        const auto internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const auto internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        const auto internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        const auto psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        const auto psram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
        const auto stack_min = uxTaskGetStackHighWaterMark(nullptr);
        ESP_LOGI(
            tag,
            "HEALTH uptime_ms=%lld sensors=%u bus=%s clock=%s history=%d heap=%u "
            "internal_free=%u internal_min=%u internal_largest=%u psram_free=%u "
            "psram_min=%u main_stack_min=%u display_dropped=%u",
            static_cast<long long>(now), static_cast<unsigned>(loop.registry().sensors().size()),
            simulated ? "SIMULATED"
                      : (sampling.enabled() ? (sampling.fault() ? "fault" : "ready")
                                            : (sampling.fault() ? "fault" : "disabled")),
            simulated ? "simulated" : (clock_synchronized() ? "synchronized" : "unsynchronized"),
            int(history.healthy()), static_cast<unsigned>(esp_get_free_heap_size()),
            static_cast<unsigned>(internal_free), static_cast<unsigned>(internal_min),
            static_cast<unsigned>(internal_largest), static_cast<unsigned>(psram_free),
            static_cast<unsigned>(psram_min), static_cast<unsigned>(stack_min),
            display_frames_dropped);
        // Numeric-only physical-bus evidence. Never log ROM IDs,
        // credentials, network names or fabricated simulated readings.
        if (sampling.enabled()) {
            ESP_LOGI(tag,
                     "SENSOR_DIAG cycles=%llu last_probes=%u max_probes=%u "
                     "valid=%llu invalid=%llu fault_cycles=%llu "
                     "last_read_ms=%lld max_read_ms=%lld",
                     static_cast<unsigned long long>(sensor_diagnostics.cycles),
                     static_cast<unsigned>(sensor_diagnostics.last_probes),
                     static_cast<unsigned>(sensor_diagnostics.max_probes),
                     static_cast<unsigned long long>(sensor_diagnostics.valid),
                     static_cast<unsigned long long>(sensor_diagnostics.invalid),
                     static_cast<unsigned long long>(sensor_diagnostics.fault_cycles),
                     static_cast<long long>(sensor_diagnostics.last_read_ms),
                     static_cast<long long>(sensor_diagnostics.max_read_ms));
        }
    }
}
} // namespace monitor
