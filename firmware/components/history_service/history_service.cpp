#include "history_service.hpp"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/task.h"
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
        if (!std::all_of(bytes.begin(), bytes.end(), [](uint8_t b) { return b == 0xff; }))
            return false;
        if (offset % 65536 == 0)
            vTaskDelay(1);
    }
    return true;
}
} // namespace
bool HistoryService::start(bool simulated) {
    simulated_ = simulated;
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_)
        return false;
    queue_ = xQueueCreate(4, sizeof(Bucket *));
    if (!queue_)
        return false;
    if (xTaskCreate(worker, "history", 12288, this, 1, nullptr) != pdPASS) {
        vQueueDelete(queue_);
        queue_ = nullptr;
        return false;
    }
    return true;
}
bool HistoryService::query(const HistoryQuery &query, std::vector<HistoryPoint> &points,
                           bool &truncated) {
    if (!valid_query(query) || !mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE)
        return false;
    HistoryReducer reducer(query);
    unsigned visited = 0;
    bool ok = store_ && store_->visit([&](const Bucket &bucket) {
        reducer.add(bucket);
        if (++visited % 32 == 0)
            vTaskDelay(1);
        return true;
    });
    xSemaphoreGive(mutex_);
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
void HistoryService::worker(void *context) {
    auto &self = *static_cast<HistoryService *>(context);
    esp_vfs_littlefs_conf_t config{};
    config.base_path = "/history";
    config.partition_label = "history";
    config.format_if_mount_failed = false;
    auto mounted = esp_vfs_littlefs_register(&config);
    if (mounted != ESP_OK && blank_partition()) {
        // Only a completely erased partition can be initialized automatically.
        // Any nonblank failed mount remains intact for explicit operator recovery.
        if (esp_littlefs_format("history") == ESP_OK)
            mounted = esp_vfs_littlefs_register(&config);
    }
    HistoryStore store(self.simulated_ ? "/history/simulation" : "/history/temperature",
                       self.simulated_ ? 64 * 1024 : 3 * 1024 * 1024,
                       self.simulated_ ? 8 * 1024 : 32 * 1024);
    const bool usable = mounted == ESP_OK && store.recover();
    xSemaphoreTake(self.mutex_, portMAX_DELAY);
    self.store_ = usable ? &store : nullptr;
    xSemaphoreGive(self.mutex_);
    self.healthy_ = usable;
    ESP_LOGI("history", "mount=%s healthy=%d; nonblank data never auto-formatted",
             esp_err_to_name(mounted), int(self.healthy_.load()));
    while (true) {
        Bucket *bucket = nullptr;
        if (xQueueReceive(self.queue_, &bucket, portMAX_DELAY) == pdTRUE && bucket) {
            xSemaphoreTake(self.mutex_, portMAX_DELAY);
            bool ok = usable && store.append(*bucket);
            xSemaphoreGive(self.mutex_);
            self.healthy_ = ok;
            if (!ok)
                ESP_LOGE("history", "Aggregate write unavailable; acquisition continues");
            delete bucket;
        }
    }
}
} // namespace monitor
