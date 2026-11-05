#pragma once
#include "FreeRTOS.h"
// Runs `fn(arg)` on a new thread; the function ends by calling vTaskDelete(nullptr).
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
                                   int priority, TaskHandle_t *handle, int core);
void vTaskDelete(TaskHandle_t handle);
