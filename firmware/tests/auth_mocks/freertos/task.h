#pragma once
#include "FreeRTOS.h"
// Runs `fn(arg)` on a new thread. The worker ends by suspending itself; on the host that call
// returns and the thread exits.
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
                                   int priority, TaskHandle_t *handle, int core);
BaseType_t xTaskCreatePinnedToCoreWithCaps(void (*fn)(void *), const char *name, uint32_t stack,
                                           void *arg, int priority, TaskHandle_t *handle, int core,
                                           uint32_t caps);
void vTaskDelete(TaskHandle_t handle);
void vTaskDeleteWithCaps(TaskHandle_t handle);
void vTaskSuspend(TaskHandle_t handle);
void vTaskDelay(uint32_t ticks);
enum eTaskState { eRunning, eReady, eBlocked, eSuspended, eDeleted, eInvalid };
eTaskState eTaskGetState(TaskHandle_t handle);
// Test hooks: make the next task creations fail (as when no memory is free), and see the memory
// capabilities the last task asked for.
struct FakeTasks {
    static bool fail_create;
    static uint32_t last_caps;
};
