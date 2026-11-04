#pragma once
#include "monitor_types.hpp"

namespace monitor {
bool can_retire_sensor(State state, uint64_t rom, const ProbeScanStatus &scan, bool simulated);
} // namespace monitor
