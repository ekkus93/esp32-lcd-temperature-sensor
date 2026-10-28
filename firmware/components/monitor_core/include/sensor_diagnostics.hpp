#pragma once

#include "monitor_types.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace monitor {
// Read-only per-boot counters for physical probe qualification. Never expose
// sensor ROMs or invent data when the physical bus is disabled.
struct SensorDiagnostics {
    uint64_t cycles = 0;
    uint64_t valid = 0;
    uint64_t invalid = 0;
    uint64_t fault_cycles = 0;
    size_t last_probes = 0;
    size_t max_probes = 0;
    int64_t last_read_ms = 0;
    int64_t max_read_ms = 0;

    void observe(const std::vector<Reading> &readings, bool bus_fault, int64_t elapsed_ms) {
        ++cycles;
        last_probes = readings.size();
        max_probes = std::max(max_probes, last_probes);
        if (bus_fault)
            ++fault_cycles;
        for (const auto &reading : readings) {
            if (reading.conversion_complete && reading.celsius.has_value())
                ++valid;
            else
                ++invalid;
        }
        last_read_ms = std::max<int64_t>(0, elapsed_ms);
        max_read_ms = std::max(max_read_ms, last_read_ms);
    }
};
} // namespace monitor
