#pragma once
#include "esp_err.h"
#include "monitor.hpp"
#include "onewire_bus.h"
namespace monitor {
class OneWireService {
  public:
    esp_err_t initialize();
    std::vector<Reading> read(int64_t now_ms);
    bool fault() const {
        return fault_;
    }
    bool enabled() const {
        return bus_ != nullptr;
    }

  private:
    onewire_bus_handle_t bus_ = nullptr;
    std::vector<uint64_t> roms_;
    int64_t next_scan_ = 0;
    bool fault_ = false;
};
} // namespace monitor
