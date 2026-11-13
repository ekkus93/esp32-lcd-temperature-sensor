#pragma once
#include "cJSON.h"
#include "monitor_types.hpp"
#include <optional>
namespace monitor {
// Rules for PUT /sensors/{rom}/calibration-hold (spec 17.3, review audit H09), used by the HTTP
// handler and tested on the host.
//
// Only a probe that is reading (normal, high or low) can be held: a disconnected, stale, invalid,
// disabled, unassigned or bus-fault probe is answered 409.
bool probe_can_be_held(State state);
// The body must be exactly {"minutes": n} with n a whole number from 1 to 20. Anything else
// (missing, extra keys, fractions, strings, booleans, out of range) gives nothing: answered 400.
std::optional<int> calibration_hold_minutes(const cJSON *body);
} // namespace monitor
