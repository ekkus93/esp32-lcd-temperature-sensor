#pragma once

#include "monitor_types.hpp"

namespace monitor {
class AlertService {
  public:
    std::optional<Event> update(Sensor &sensor, const Reading *reading, bool bus_fault,
                                int64_t now_ms) const;
    std::optional<Event> transition(Sensor &sensor, State state, int64_t now_ms) const;
};
} // namespace monitor
