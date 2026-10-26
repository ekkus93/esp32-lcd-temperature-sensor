#pragma once
#include "device_options.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
namespace monitor {
class DeviceSettingsService {
  public:
    esp_err_t initialize(bool simulated = false);
    bool get(DeviceOptions &options, uint64_t &revision, bool &writable);
    esp_err_t save(const DeviceOptions &options, uint64_t expected_revision);

  private:
    SemaphoreHandle_t mutex_ = nullptr;
    const char *namespace_ = "monitor_device";
    DeviceOptions options_;
    uint64_t revision_ = 0;
    bool writable_ = false;
};
} // namespace monitor
