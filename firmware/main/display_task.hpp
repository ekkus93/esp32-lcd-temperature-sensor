#pragma once
// The LCD's own 100 ms refresh task, and the main loop's hand-over to it (God file refactor G15).
#include "display_runtime.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "http_api.hpp"
#include "loop_ports.hpp"
#include "wifi_service.hpp"
namespace monitor {
struct DisplayTaskContext {
    DisplayRuntime *display;
    HttpApi *api;
    WifiService *wifi;
    SemaphoreHandle_t mutex;
    bool api_publish_pending = false;
    DisplayFrame frame;
};
// Redraws every 100 ms from the last frame, and republishes the snapshot when it expires.
void display_refresh_task(void *argument);
} // namespace monitor
