#pragma once

#include "esp_err.h"
#include "monitor.hpp"
#include "onewire_service.hpp"

namespace monitor {
class SamplingService {
  public:
    esp_err_t initialize(bool simulated);
    std::vector<Reading> read(int64_t now_ms, const std::vector<uint64_t> &managed);
    bool fault() const;
    bool enabled() const;
    // False only for a build that cannot read probes at all (sensor bus compiled out); a driver
    // that failed to start counts as monitored, and shows as a bus fault.
    bool monitored() const;
    ProbeScanStatus scan_status() const {
        auto status = simulated_ ? ProbeScanStatus{} : bus_.scan_status();
        status.enabled = enabled();
        return status;
    }

  private:
    OneWireService bus_;
    bool simulated_ = false;
};
} // namespace monitor
