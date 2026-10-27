#pragma once

#include "esp_err.h"
#include "monitor.hpp"
#include "onewire_service.hpp"

namespace monitor {
class SamplingService {
  public:
    esp_err_t initialize(bool simulated);
    std::vector<Reading> read(int64_t now_ms);
    bool fault() const;
    bool enabled() const;

  private:
    OneWireService bus_;
    bool simulated_ = false;
};
} // namespace monitor
