#include "history_service.hpp"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "history_mount_policy.hpp"
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
        return false;
    }
    if (xTaskCreate(worker, "history", 12288, this, 1, nullptr) != pdPASS) {
        vQueueDelete(queue_);
        vQueueDelete(event_queue_);
        queue_ = nullptr;
        event_queue_ = nullptr;
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
                           bool &truncated) {
    if (!valid_query(query)) {
        ESP_LOGW("history", "Rejected invalid history query");
        return false;
    }
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW("history", "History query mutex busy");
        return false;
    }
    HistoryReducer reducer(query);
    unsigned visited = 0;
    bool ok = store_ && store_->visit([&](const Bucket &bucket) {
        reducer.add(bucket);
        if (++visited % 32 == 0)
            vTaskDelay(1);
        return true;
    });
    xSemaphoreGive(mutex_);
    if (!ok)
        ESP_LOGE("history", "History query storage iteration failed");
    if (ok) {
        points = reducer.finish();
        truncated = reducer.truncated();
    }
    return ok;
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
bool HistoryService::visit(const HistoryQuery &query,
                           const std::function<bool(const Bucket &)> &visitor) {
    if (!valid_query(query) || !mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE)
        return false;
    const int64_t deadline = esp_timer_get_time() / 1000 + 15000;
    bool complete = true;
    unsigned visited = 0;
    bool ok = store_ && store_->visit([&](const Bucket &bucket) {
        if (++visited % 32 == 0)
            vTaskDelay(1);
        if (esp_timer_get_time() / 1000 >= deadline) {
            complete = false;
            return false;
        }
        int64_t time = bucket.start_uptime_ms;
        if (query.boot_id) {
            if (bucket.boot_id != *query.boot_id)
                return true;
        } else {
            if (!bucket.utc_seconds || *bucket.utc_seconds < 0 ||
                *bucket.utc_seconds > INT64_MAX / 1000)
                return true;
            time = *bucket.utc_seconds * 1000;
        }
        if (time < query.from || time >= query.to)
            return true;
        if (!visitor(bucket)) {
            complete = false;
            return false;
        }
        return true;
    });
    xSemaphoreGive(mutex_);
    return ok && complete;
}
HistoryStorageInfo HistoryService::storage_info() {
    HistoryStorageInfo info{};
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE)
        return info;
    if (store_ && healthy_.load()) {
        info.available =
            esp_littlefs_info("history", &info.total_bytes, &info.used_bytes) == ESP_OK;
        if (info.available && info.used_bytes > info.total_bytes)
            info.available = false;
    }
    xSemaphoreGive(mutex_);
    if (!info.available)
        return {};
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
    while (true) {
        Bucket *bucket = nullptr;
        if (xQueueReceive(self.queue_, &bucket, pdMS_TO_TICKS(100)) == pdTRUE && bucket) {
            xSemaphoreTake(self.mutex_, portMAX_DELAY);
            bool ok = usable && store.append(*bucket);
            xSemaphoreGive(self.mutex_);
            self.healthy_ = ok;
            if (!ok) {
                ESP_LOGE("history", "Aggregate write unavailable; acquisition continues");
                JournalEvent failure{};
                failure.boot_id = self.boot_id_;
                failure.uptime_ms = esp_timer_get_time() / 1000;
                failure.kind = EventKind::storage_error;
                failure.detail = 1;
                xSemaphoreTake(self.mutex_, portMAX_DELAY);
                const bool recorded = events_usable && event_store.append(failure);
                xSemaphoreGive(self.mutex_);
                if (!recorded)
                    self.event_healthy_ = false;
            }
            delete bucket;
        }
        JournalEvent *event = nullptr;
        while (xQueueReceive(self.event_queue_, &event, 0) == pdTRUE && event) {
            xSemaphoreTake(self.mutex_, portMAX_DELAY);
            const bool ok = events_usable && event_store.append(*event);
            xSemaphoreGive(self.mutex_);
            if (!ok) {
                self.event_healthy_ = false;
                ESP_LOGE("history", "Event journal write failed");
            }
            delete event;
        }
    }
}
} // namespace monitor
