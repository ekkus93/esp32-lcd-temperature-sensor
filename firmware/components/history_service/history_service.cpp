#include "history_service.hpp"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "history_mount_policy.hpp"
#include "storage_error_limiter.hpp"
#include <algorithm>
#include <array>
#include <new>
namespace monitor {
namespace {
bool blank_partition() {
    auto *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "history");
    if (!partition)
        return false;
    std::array<uint8_t, 4096> bytes{};
    for (size_t offset = 0; offset < partition->size; offset += bytes.size()) {
        if (esp_partition_read(partition, offset, bytes.data(), bytes.size()) != ESP_OK)
            return false;
        if (!history_partition_erased(bytes.data(), bytes.size()))
            return false;
        if (offset % 65536 == 0)
            vTaskDelay(1);
    }
    return true;
}
} // namespace
bool HistoryService::start(bool simulated, uint64_t boot_id) {
    simulated_ = simulated;
    boot_id_ = boot_id;
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_)
        return false;
    queue_ = xQueueCreate(4, sizeof(Bucket *));
    event_queue_ = xQueueCreate(16, sizeof(JournalEvent *));
    if (!queue_ || !event_queue_) {
        if (queue_)
            vQueueDelete(queue_);
        if (event_queue_)
            vQueueDelete(event_queue_);
        queue_ = nullptr;
        event_queue_ = nullptr;
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
        return false;
    }
    if (xTaskCreate(worker, "history", 12288, this, 1, nullptr) != pdPASS) {
        vQueueDelete(queue_);
        vQueueDelete(event_queue_);
        queue_ = nullptr;
        event_queue_ = nullptr;
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
        return false;
    }
    return true;
}
bool HistoryService::submit_event(const JournalEvent &event) {
    if (!event_queue_)
        return false;
    auto *copy = new (std::nothrow) JournalEvent(event);
    if (!copy)
        return false;
    if (xQueueSend(event_queue_, &copy, 0) != pdTRUE) {
        delete copy;
        event_healthy_ = false;
        return false;
    }
    return true;
}
JournalPage HistoryService::event_page(uint64_t after, size_t limit) {
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE)
        return {};
    auto page = event_store_ ? event_store_->page(after, limit) : JournalPage{};
    page.available = page.available && event_healthy_.load();
    xSemaphoreGive(mutex_);
    return page;
}
bool HistoryService::query(const HistoryQuery &query, std::vector<HistoryPoint> &points,
                           std::vector<HistoryGap> &gaps, bool &truncated) {
    if (!valid_query(query)) {
        ESP_LOGW("history", "Rejected invalid history query");
        return false;
    }
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW("history", "History query mutex busy");
        return false;
    }
    // A query runs on the single HTTP task with the history mutex held, so it gets a time budget:
    // past it, delivery stops and the answer is marked truncated instead of holding up the server.
    constexpr int64_t query_budget_us = 3000000;
    const int64_t deadline_us = esp_timer_get_time() + query_budget_us;
    HistoryVisitOptions options;
    options.expired = [&]() { return esp_timer_get_time() > deadline_us; };
    options.pace = []() { vTaskDelay(1); };
    HistoryQueryAnswer answer;
    if (store_)
        answer = answer_history_query(*store_, query, options);
    xSemaphoreGive(mutex_);
    if (answer.visit.skipped_segments)
        ESP_LOGW("history", "%u damaged history segment(s) skipped; answer is truncated",
                 unsigned(answer.visit.skipped_segments));
    if (!answer.ok())
        ESP_LOGE("history", "History query storage iteration failed");
    if (answer.visit.end == HistoryVisitEnd::deadline)
        ESP_LOGW("history", "History query stopped at its time budget; answer is truncated");
    if (!answer.ok())
        return false;
    points = std::move(answer.points);
    gaps = std::move(answer.gaps);
    truncated = answer.truncated;
    return true;
}
bool HistoryService::submit(const Bucket &bucket) {
    if (!queue_)
        return false;
    auto *copy = new (std::nothrow) Bucket(bucket);
    if (!copy)
        return false;
    if (xQueueSend(queue_, &copy, 0) != pdTRUE) {
        delete copy;
        healthy_ = false;
        return false;
    }
    return true;
}
HistoryVisitOutcome HistoryService::visit(const HistoryQuery &query,
                                          const std::function<bool(const Bucket &)> &visitor,
                                          const HistoryVisitOptions &options) {
    if (!valid_query(query) || !mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE)
        return {};
    const int64_t deadline = esp_timer_get_time() / 1000 + 15000;
    HistoryVisitOptions service_options;
    service_options.expired = [&]() { return esp_timer_get_time() / 1000 >= deadline; };
    service_options.pace = []() { vTaskDelay(1); };
    service_options.stop_on_damage = options.stop_on_damage;
    const HistoryRange range{query.boot_id, query.from, query.to};
    HistoryVisitOutcome outcome;
    if (store_)
        outcome = visit_history_range(*store_, range, visitor, service_options);
    xSemaphoreGive(mutex_);
    if (outcome.skipped_segments)
        ESP_LOGW("history", "%u damaged history segment(s) skipped; export is incomplete",
                 unsigned(outcome.skipped_segments));
    return outcome;
}
HistoryStorageInfo HistoryService::storage_info() {
    HistoryStorageInfo info{};
    if (!mutex_)
        return info;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE) {
        // Busy, not broken: keep the last known figures.
        std::lock_guard<std::mutex> lock(info_mutex_);
        return last_info_;
    }
    if (store_ && healthy_.load()) {
        info.available =
            esp_littlefs_info("history", &info.total_bytes, &info.used_bytes) == ESP_OK;
        if (info.available && info.used_bytes > info.total_bytes)
            info.available = false;
    }
    xSemaphoreGive(mutex_);
    if (!info.available)
        info = {};
    std::lock_guard<std::mutex> lock(info_mutex_);
    last_info_ = info;
    return info;
}
void HistoryService::worker(void *context) {
    auto &self = *static_cast<HistoryService *>(context);
    esp_vfs_littlefs_conf_t config{};
    config.base_path = "/history";
    config.partition_label = "history";
    config.format_if_mount_failed = false;
    auto mounted = esp_vfs_littlefs_register(&config);
    if (may_format_history_partition(mounted != ESP_OK, blank_partition())) {
        // Only a completely erased partition can be initialized automatically.
        // Any nonblank failed mount remains intact for explicit operator recovery.
        if (esp_littlefs_format("history") == ESP_OK)
            mounted = esp_vfs_littlefs_register(&config);
    }
    HistoryStore store(self.simulated_ ? "/history/simulation" : "/history/temperature",
                       self.simulated_ ? 64 * 1024 : 3 * 1024 * 1024,
                       self.simulated_ ? 8 * 1024 : 32 * 1024);
    const bool usable = mounted == ESP_OK && store.recover();
    EventStore event_store(self.simulated_ ? "/history/simulation/events.bin"
                                           : "/history/events.bin");
    const bool events_usable = mounted == ESP_OK && event_store.recover();
    xSemaphoreTake(self.mutex_, portMAX_DELAY);
    self.store_ = usable ? &store : nullptr;
    self.event_store_ = events_usable ? &event_store : nullptr;
    xSemaphoreGive(self.mutex_);
    self.healthy_ = usable;
    self.event_healthy_ = events_usable;
    ESP_LOGI("history", "mount=%s healthy=%d; nonblank data never auto-formatted",
             esp_err_to_name(mounted), int(self.healthy_.load()));
    ESP_LOGI("history", "event_log_healthy=%d", int(self.event_healthy_.load()));
    StorageErrorLimiter error_limiter;
    while (true) {
        Bucket *bucket = nullptr;
        if (xQueueReceive(self.queue_, &bucket, pdMS_TO_TICKS(100)) == pdTRUE && bucket) {
            xSemaphoreTake(self.mutex_, portMAX_DELAY);
            bool ok = usable && store.append(*bucket);
            xSemaphoreGive(self.mutex_);
            self.healthy_ = ok;
            const int64_t now_ms = esp_timer_get_time() / 1000;
            if (!ok)
                ESP_LOGE("history", "Aggregate write unavailable; acquisition continues");
            if (error_limiter.should_record(ok, now_ms)) {
                JournalEvent failure{};
                failure.boot_id = self.boot_id_;
                failure.uptime_ms = now_ms;
                failure.kind = EventKind::storage_error;
                failure.detail = 1;
                xSemaphoreTake(self.mutex_, portMAX_DELAY);
                const bool recorded = events_usable && event_store.append(failure);
                xSemaphoreGive(self.mutex_);
                self.event_healthy_ = recorded;
            }
            delete bucket;
        }
        JournalEvent *event = nullptr;
        while (xQueueReceive(self.event_queue_, &event, 0) == pdTRUE && event) {
            xSemaphoreTake(self.mutex_, portMAX_DELAY);
            const bool ok = events_usable && event_store.append(*event);
            xSemaphoreGive(self.mutex_);
            // The flag follows the latest write, so it recovers once the journal writes again.
            self.event_healthy_ = ok;
            if (!ok)
                ESP_LOGE("history", "Event journal write failed");
            delete event;
        }
    }
}
} // namespace monitor
