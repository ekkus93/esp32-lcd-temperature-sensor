#pragma once
// The one-shot task that reads the last day of stored buckets for the LCD trend graph (God file
// refactor G15).
#include "display_model.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "history_service.hpp"
namespace monitor {
// The last day of stored buckets for the trend graph. The history worker owns the store and a
// visit can take seconds, so it runs in its own task; the main loop places the buckets on this
// boot's timeline once the clock is trusted (the stored UTC starts are what places them).
struct TrendRestore {
    HistoryService *history = nullptr;
    SemaphoreHandle_t mutex = nullptr;
    TrendPoints stored;                    // guarded by mutex; written by the task
    bool done = false;                     // guarded by mutex
    bool started = false, applied = false; // main loop only
};

void trend_restore_task(void *argument);
} // namespace monitor
