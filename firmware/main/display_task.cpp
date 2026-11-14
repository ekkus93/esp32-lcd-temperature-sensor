// The LCD's own refresh task (moved from main.cpp, God file refactor G15).
#include "display_task.hpp"
#include "display_model.hpp"
#include "esp_timer.h"
#include "freertos/task.h"
#include "provisioning_service.hpp"
#include <algorithm>
namespace monitor {
void display_refresh_task(void *argument) {
    auto &context = *static_cast<DisplayTaskContext *>(argument);
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        if (xSemaphoreTake(context.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            context.frame.status.button_progress = boot_hold_progress();
            // Read directly so the setup screen appears within one 100 ms refresh of the AP
            // opening, not at the next 5 s acquisition frame.
            context.frame.provisioning = context.wifi && context.wifi->provisioning_open();
            const auto now_ms = esp_timer_get_time() / 1000;
            const auto uptime_ms = context.frame.uptime_ms +
                                   std::max<int64_t>(0, now_ms - context.frame.published_at_ms);
            context.api_publish_pending |=
                expire_display_snapshot(context.frame.registry, uptime_ms);
            if (context.api_publish_pending &&
                context.api->publish(context.frame.registry, context.frame.scan_status, uptime_ms,
                                     context.frame.logging, context.frame.synchronized,
                                     context.frame.utc))
                context.api_publish_pending = false;
            context.display->update(context.frame.registry, *context.frame.trend_cache,
                                    context.frame.options, context.frame.status, uptime_ms,
                                    context.frame.provisioning, context.frame.ssid,
                                    context.frame.wifi_key, context.frame.local_name);
            xSemaphoreGive(context.mutex);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(100));
    }
}
} // namespace monitor
