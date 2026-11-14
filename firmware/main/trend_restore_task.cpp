// The trend graph's history restore (moved from main.cpp, God file refactor G15).
#include "trend_restore_task.hpp"
#include "clock_service.hpp"
#include "esp_log.h"
#include "freertos/task.h"
namespace monitor {
void trend_restore_task(void *argument) {
    auto &restore = *static_cast<TrendRestore *>(argument);
    // Only what the graph draws is kept, in PSRAM: a day of full buckets used most of the free
    // internal RAM while it was collected.
    TrendPoints collected;
    if (const auto utc = trusted_utc()) {
        HistoryQuery query;
        query.from = (*utc - 86400) * 1000;
        query.to = (*utc + 1) * 1000;
        const auto outcome = restore.history->visit(query, [&](const Bucket &bucket) {
            collected.push_back(trend_point(bucket));
            return true;
        });
        if (!outcome.complete())
            ESP_LOGW("trends",
                     "Trend history restore read only part of the stored history (end %u, %u "
                     "damaged segment(s))",
                     unsigned(outcome.end), unsigned(outcome.skipped_segments));
    } else {
        ESP_LOGW("trends", "Trend history restore skipped: the clock is not trusted");
    }
    if (xSemaphoreTake(restore.mutex, portMAX_DELAY) == pdTRUE) {
        restore.stored = std::move(collected);
        restore.done = true;
        xSemaphoreGive(restore.mutex);
    }
    vTaskDelete(nullptr);
}
} // namespace monitor
