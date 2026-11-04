#pragma once
#include "esp_err.h"
#include "monitor.hpp"
#include "onewire_bus.h"
#include "probe_discovery.hpp"
namespace monitor {
class OneWireService {
  public:
    esp_err_t initialize();
    std::vector<Reading> read(int64_t now_ms, const std::vector<uint64_t> &managed);
    ProbeScanStatus scan_status() const {
        return scan_status_;
    }
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
    ProbeScanStatus scan_status_{};
};
} // namespace monitor
