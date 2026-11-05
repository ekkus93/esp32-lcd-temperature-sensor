#pragma once
#include <cstdint>
using BaseType_t = int;
using TaskHandle_t = void *;
using SemaphoreHandle_t = void *;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdPASS = 1;
constexpr uint32_t portMAX_DELAY = 0xffffffffu;
constexpr int tskIDLE_PRIORITY = 0;
