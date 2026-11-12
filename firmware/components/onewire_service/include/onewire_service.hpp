#pragma once
#include "esp_err.h"
#include "monitor.hpp"
#include "onewire_bus.h"
#include "scan_policy.hpp"
namespace monitor {
// Talks to the 1-Wire driver. When to rescan, which ROMs to read and when the bus is faulty are
// decided by ScanPolicy, which has host tests.
class OneWireService {
  public:
    esp_err_t initialize();
    std::vector<Reading> read(int64_t now_ms, const std::vector<uint64_t> &managed);
    // Writes `memory` to the probe's TH and TL bytes (with 12-bit resolution) and copies them to
    // its EEPROM, then reloads the EEPROM and returns what it holds; empty if the bus failed.
    // Takes about 30 ms. Call between reads, never during a conversion.
    std::optional<ProbeMemory> store(uint64_t rom, ProbeMemory memory);
    ProbeScanStatus scan_status() const {
        return policy_.status();
    }
    bool fault() const {
        return init_failed_ || policy_.fault();
    }
    bool enabled() const {
        return bus_ != nullptr;
    }
    // True if the driver was meant to run but could not start; that is a bus fault, whereas a
    // build with the bus compiled out simply cannot read probes.
    bool init_failed() const {
        return init_failed_;
    }

  private:
    bool select(uint64_t rom);
    std::optional<std::array<uint8_t, 9>> read_scratchpad(uint64_t rom);
    onewire_bus_handle_t bus_ = nullptr;
    bool init_failed_ = false;
    ScanPolicy policy_;
};
} // namespace monitor
