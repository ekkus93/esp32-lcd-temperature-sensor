#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "history.hpp"
#include "history_query.hpp"
#include <atomic>
namespace monitor {
class HistoryService {
  public:
    bool start(bool simulated = false);
    bool submit(const Bucket &bucket);
    bool query(const HistoryQuery &query, std::vector<HistoryPoint> &points, bool &truncated);
    bool visit(const HistoryQuery &query, const std::function<bool(const Bucket &)> &visitor);
    bool healthy() const {
        return healthy_.load();
    }

  private:
    static void worker(void *context);
    QueueHandle_t queue_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    HistoryStore *store_ = nullptr; // owned by the lifetime-long worker; protected by mutex_
    bool simulated_ = false;
    std::atomic<bool> healthy_{false};
};
} // namespace monitor
