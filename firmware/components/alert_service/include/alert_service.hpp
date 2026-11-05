#pragma once

#include "monitor_types.hpp"

namespace monitor {
class AlertService {
  public:
    // `monitored` is false when nothing can read probes (sensor bus compiled out): the sensor is
    // shown as disabled and raises no alarm.
    std::optional<Event> update(Sensor &sensor, const Reading *reading, bool bus_fault,
                                int64_t now_ms, bool monitored = true) const;
    std::optional<Event> transition(Sensor &sensor, State state, int64_t now_ms) const;
};
} // namespace monitor
