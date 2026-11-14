#pragma once
// Logging is silent in the host loop tests.
#include "esp_err.h"
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGD(tag, ...) ((void)(tag))
inline const char *esp_err_to_name(esp_err_t) {
    return "error";
}
