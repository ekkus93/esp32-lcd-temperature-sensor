#pragma once
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
constexpr esp_err_t ESP_ERR_NO_MEM = 0x101;
constexpr esp_err_t ESP_ERR_INVALID_ARG = 0x102;
constexpr esp_err_t ESP_ERR_INVALID_SIZE = 0x103;
constexpr esp_err_t ESP_ERR_INVALID_STATE = 0x104;
constexpr esp_err_t ESP_ERR_NVS_BASE = 0x1100;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = ESP_ERR_NVS_BASE + 0x02;
constexpr esp_err_t ESP_ERR_NVS_INVALID_LENGTH = ESP_ERR_NVS_BASE + 0x0c;
