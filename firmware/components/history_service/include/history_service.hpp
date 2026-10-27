#pragma once
#include "event_store.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "history.hpp"
#include "history_query.hpp"
#include <atomic>
namespace monitor {
struct HistoryStorageInfo {
    size_t total_bytes = 0;
    size_t used_bytes = 0;
    bool available = false;
};
class HistoryService {
  public:
    bool start(bool simulated = false, uint64_t boot_id = 0);
    bool submit(const Bucket &bucket);
    bool submit_event(const JournalEvent &event);
    JournalPage event_page(uint64_t after, size_t limit);
    bool query(const HistoryQuery &query, std::vector<HistoryPoint> &points, bool &truncated);
    bool visit(const HistoryQuery &query, const std::function<bool(const Bucket &)> &visitor);
    HistoryStorageInfo storage_info();
    bool healthy() const {
        return healthy_.load();
    }

  private:
    static void worker(void *context);
    QueueHandle_t queue_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    HistoryStore *store_ = nullptr; // owned by the lifetime-long worker; protected by mutex_
    bool simulated_ = false;
    uint64_t boot_id_ = 0;
    EventStore *event_store_ = nullptr; // worker lifetime; protected by mutex_
    QueueHandle_t event_queue_ = nullptr;
    std::atomic<bool> healthy_{false};
    std::atomic<bool> event_healthy_{false};
};
} // namespace monitor
