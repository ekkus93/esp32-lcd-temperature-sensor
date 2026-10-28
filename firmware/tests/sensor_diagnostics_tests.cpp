#include "sensor_diagnostics.hpp"

#include <cassert>
#include <iostream>
#include <optional>
#include <vector>

int main() {
    using monitor::Reading;
    monitor::SensorDiagnostics stats;
    assert(stats.cycles == 0 && stats.valid == 0 && stats.invalid == 0);
    stats.observe({Reading{1, 20.5, true}}, false, 752);
    assert(stats.cycles == 1 && stats.valid == 1 && stats.invalid == 0);
    assert(stats.last_probes == 1 && stats.max_probes == 1);
    assert(stats.last_read_ms == 752 && stats.max_read_ms == 752);
    std::vector<Reading> five;
    five.push_back({1, 20.0, true});
    five.push_back({2, 21.0, true});
    five.push_back({3, std::nullopt, true});
    five.push_back({4, 23.0, true});
    five.push_back({5, std::nullopt, false});
    stats.observe(five, false, 785);
    assert(stats.cycles == 2 && stats.valid == 4 && stats.invalid == 2);
    assert(stats.last_probes == 5 && stats.max_probes == 5);
    assert(stats.last_read_ms == 785 && stats.max_read_ms == 785);
    stats.observe({}, true, 1);
    assert(stats.cycles == 3 && stats.fault_cycles == 1);
    assert(stats.last_probes == 0 && stats.max_probes == 5);
    assert(stats.last_read_ms == 1 && stats.max_read_ms == 785);
    stats.observe({}, false, -100);
    assert(stats.last_read_ms == 0 && stats.max_read_ms == 785);
    std::cout << "PASS: physical sensor diagnostics counts, faults and read duration\n";
}
