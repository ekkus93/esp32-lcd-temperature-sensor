#include "sensor_registry.hpp"
#include <algorithm>

namespace monitor {
Sensor *Registry::find(uint64_t rom) {
    auto it = std::find_if(sensors_.begin(), sensors_.end(),
                           [rom](const Sensor &s) { return s.rom == rom; });
    return it == sensors_.end() ? nullptr : &*it;
}
bool Registry::discover(uint64_t rom) {
    if (!valid_rom(rom))
        return false;
    if (find(rom))
        return true;
    if (sensors_.size() == max_sensors) {
        ++excess_;
        return false;
    }
    Sensor sensor;
    sensor.rom = rom;
    sensor.settings.order = sensors_.size();
    sensor.settings.name = "Sensor " + std::to_string(sensors_.size() + 1);
    sensor.state = State::invalid;
    sensors_.push_back(sensor);
    return true;
}
bool Registry::configure(uint64_t rom, const Settings &settings, std::optional<int64_t> now_ms) {
    auto *sensor = find(rom);
    if (!sensor || !valid_settings(settings))
        return false;
    const auto previous = sensor->settings;
    const bool behavior_changed = previous.enabled != settings.enabled ||
                                  previous.calibration != settings.calibration ||
                                  previous.low != settings.low || previous.high != settings.high ||
                                  previous.hysteresis != settings.hysteresis ||
                                  previous.persistence_ms != settings.persistence_ms;
    sensor->settings = settings;
    ++sensor->config_revision;
    if (behavior_changed) {
        sensor->pending_since.reset();
        sensor->failures = 0;
        sensor->temperature.reset();
        transition(*sensor, settings.enabled ? State::invalid : State::disabled,
                   now_ms.value_or(sensor->last_seen_ms.value_or(0)));
    }
    ++sequence_;
    return true;
}

void Registry::event(uint64_t rom, State previous, State state, int64_t now_ms) {
    record({0, rom, previous, state, now_ms});
}
void Registry::record(Event event) {
    if (events_.size() == 128)
        events_.erase(events_.begin());
    event.sequence = ++event_sequence_;
    events_.push_back(event);
}
void Registry::transition(Sensor &sensor, State state, int64_t now_ms) {
    if (auto transition = alerts_.transition(sensor, state, now_ms))
        record(*transition);
}
void Registry::sample(const std::vector<Reading> &readings, int64_t now_ms, bool fault) {
    ++sequence_;
    if (fault != bus_fault_)
        event(0, bus_fault_ ? State::bus_fault : State::normal,
              fault ? State::bus_fault : State::normal, now_ms);
    bus_fault_ = fault;
    // Sort new identities so discovery permutations yield deterministic initial names.
    std::vector<uint64_t> discovered;
    for (const auto &r : readings)
        if (valid_rom(r.rom))
            discovered.push_back(r.rom);
    std::sort(discovered.begin(), discovered.end());
    discovered.erase(std::unique(discovered.begin(), discovered.end()), discovered.end());
    excess_ = 0;
    for (auto rom : discovered)
        discover(rom);
    for (auto &sensor : sensors_) {
        const auto it = std::find_if(readings.begin(), readings.end(),
                                     [&](const Reading &r) { return r.rom == sensor.rom; });
        auto generated =
            alerts_.update(sensor, it == readings.end() ? nullptr : &*it, fault, now_ms);
        if (generated)
            record(*generated);
    }
}
void Registry::expire(int64_t now_ms) {
    for (auto &sensor : sensors_) {
        if (sensor.settings.enabled && sensor.last_seen_ms &&
            now_ms - *sensor.last_seen_ms > sample_ms * 3 && sensor.state != State::bus_fault &&
            sensor.state != State::disconnected) {
            sensor.temperature.reset();
            transition(sensor, State::stale, now_ms);
            ++sequence_;
        }
    }
}
const std::vector<Sensor> &Registry::sensors() const {
    return sensors_;
}
const std::vector<Event> &Registry::events() const {
    return events_;
}
uint64_t Registry::sequence() const {
    return sequence_;
}
unsigned Registry::excess_count() const {
    return excess_;
}
bool Registry::bus_fault() const {
    return bus_fault_;
}
} // namespace monitor
