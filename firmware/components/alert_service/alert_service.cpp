#include "alert_service.hpp"
#include <cmath>

namespace monitor {
std::optional<Event> AlertService::transition(Sensor &sensor, State state, int64_t now_ms) const {
    if (sensor.state == state)
        return std::nullopt;
    const State previous = sensor.state;
    const auto is_alert = [](State value) {
        return value == State::high || value == State::low || value == State::disconnected;
    };
    sensor.state = state;
    if ((is_alert(previous) || is_alert(state)) &&
        (state != State::disconnected || sensor.settings.disconnect_alert))
        return Event{0, sensor.rom, previous, state, now_ms};
    return std::nullopt;
}

std::optional<Event> AlertService::update(Sensor &sensor, const Reading *reading, bool bus_fault,
                                          int64_t now_ms) const {
    if (!sensor.settings.enabled) {
        sensor.temperature.reset();
        sensor.pending_since.reset();
        return transition(sensor, State::disabled, now_ms);
    }
    if (bus_fault) {
        sensor.state = State::bus_fault;
        sensor.temperature.reset();
        sensor.pending_since.reset();
        return std::nullopt;
    }
    const bool valid = reading && reading->conversion_complete && reading->celsius &&
                       std::isfinite(*reading->celsius) && *reading->celsius >= -55 &&
                       *reading->celsius <= 125;
    if (!valid) {
        sensor.temperature.reset();
        sensor.pending_since.reset();
        if (reading)
            sensor.failures = 0;
        else if (sensor.failures < 3)
            ++sensor.failures;
        return transition(sensor, sensor.failures >= 3 ? State::disconnected : State::invalid,
                          now_ms);
    }

    sensor.failures = 0;
    sensor.last_seen_ms = now_ms;
    sensor.temperature = *reading->celsius + sensor.settings.calibration;
    const double value = *sensor.temperature;
    State target = State::normal;
    if (sensor.settings.high && (value > *sensor.settings.high ||
                                 (sensor.state == State::high &&
                                  value > *sensor.settings.high - sensor.settings.hysteresis)))
        target = State::high;
    else if (sensor.settings.low && (value < *sensor.settings.low ||
                                     (sensor.state == State::low &&
                                      value < *sensor.settings.low + sensor.settings.hysteresis)))
        target = State::low;

    if (target == State::normal || target == sensor.state) {
        sensor.pending_since.reset();
        return transition(sensor, target, now_ms);
    }
    if (!sensor.pending_since || sensor.pending != target) {
        sensor.pending = target;
        sensor.pending_since = now_ms;
    }
    if (now_ms - *sensor.pending_since >= sensor.settings.persistence_ms)
        return transition(sensor, target, now_ms);
    if (sensor.state != State::high && sensor.state != State::low)
        return transition(sensor, State::normal, now_ms);
    return std::nullopt;
}
} // namespace monitor
