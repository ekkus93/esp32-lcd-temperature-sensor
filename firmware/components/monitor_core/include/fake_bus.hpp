#pragma once
#include "monitor.hpp"
namespace monitor {
// Explicit test data provider; callers must label snapshots and isolate its storage.
std::vector<Reading> fake_readings(unsigned count = 5);
std::vector<Reading> fake_replacement_readings();
} // namespace monitor
