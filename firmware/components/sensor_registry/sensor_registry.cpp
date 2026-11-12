#include "sensor_registry.hpp"
#include <algorithm>
#include <cmath>

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
    if (sensors_.size() == max_sensors)
        return false; // overflow is reported by the probe scan, not here
    std::array<bool, max_sensors> used_orders{};
    for (const auto &existing : sensors_)
        if (existing.settings.order < used_orders.size())
            used_orders[existing.settings.order] = true;
    const auto available_order = std::find(used_orders.begin(), used_orders.end(), false);
    const auto order = static_cast<unsigned>(available_order - used_orders.begin());
    Sensor sensor;
    sensor.rom = rom;
    sensor.settings.order = order;
    sensor.settings.name = "Sensor " + std::to_string(order + 1);
    sensor.state = State::invalid;
    sensors_.push_back(sensor);
    return true;
}
bool Registry::retire(uint64_t rom) {
    auto it = std::find_if(sensors_.begin(), sensors_.end(),
                           [rom](const Sensor &sensor) { return sensor.rom == rom; });
    if (it == sensors_.end() || it->state != State::disconnected)
        return false;
    sensors_.erase(it);
    ++sequence_;
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
    sensor->adopt_probe_calibration = false;
    if (previous.calibration != settings.calibration) {
        sensor->probe_write_failures = 0;
        sensor->probe_calibration = ProbeCalibration::unknown; // drops "restored"
        refresh_probe_calibration(*sensor);
    }
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

void Registry::refresh_probe_calibration(Sensor &sensor) {
    if (!sensor.probe_memory) {
        sensor.probe_calibration = ProbeCalibration::unknown;
        return;
    }
    if (*sensor.probe_memory != encode_probe_calibration(sensor.settings.calibration)) {
        sensor.probe_calibration = sensor.probe_write_failures >= max_probe_write_attempts
                                       ? ProbeCalibration::failed
                                       : ProbeCalibration::pending;
        return;
    }
    if (std::fabs(sensor.settings.calibration) > probe_calibration_limit)
        sensor.probe_calibration = ProbeCalibration::out_of_range;
    else if (sensor.probe_calibration != ProbeCalibration::restored)
        sensor.probe_calibration = ProbeCalibration::stored;
}
std::optional<Registry::ProbeWrite> Registry::next_probe_write() const {
    for (const auto &sensor : sensors_)
        if (sensor.probe_calibration == ProbeCalibration::pending)
            return ProbeWrite{sensor.rom, encode_probe_calibration(sensor.settings.calibration)};
    return std::nullopt;
}
void Registry::probe_written(uint64_t rom, std::optional<ProbeMemory> memory) {
    auto *sensor = find(rom);
    if (!sensor)
        return;
    if (memory)
        sensor->probe_memory = memory;
    if (!memory || *memory != encode_probe_calibration(sensor->settings.calibration))
        ++sensor->probe_write_failures;
    refresh_probe_calibration(*sensor);
    ++sequence_;
}

void Registry::event(uint64_t rom, State previous, State state, int64_t now_ms) {
    record({0, rom, previous, state, now_ms});
}
void Registry::record(Event event) {
    const auto same = std::find_if(recorded_.begin(), recorded_.end(), [&](const Recorded &r) {
        return r.rom == event.rom && r.previous == event.previous && r.state == event.state;
    });
    if (same != recorded_.end()) {
        if (event.uptime_ms >= same->uptime_ms &&
            event.uptime_ms - same->uptime_ms < journal_repeat_ms)
            return;
        same->uptime_ms = event.uptime_ms;
    } else {
        if (recorded_.size() == max_recorded)
            recorded_.erase(recorded_.begin());
        recorded_.push_back({event.rom, event.previous, event.state, event.uptime_ms});
    }
    if (events_.size() == 128)
        events_.erase(events_.begin());
    event.sequence = ++event_sequence_;
    events_.push_back(event);
}
void Registry::transition(Sensor &sensor, State state, int64_t now_ms) {
    if (auto transition = alerts_.transition(sensor, state, now_ms))
        record(*transition);
}
bool Registry::sample(const std::vector<Reading> &readings, int64_t now_ms, bool fault,
                      bool monitored) {
    bool settings_changed = false;
    ++sequence_;
    if (fault != bus_fault_)
        event(0, bus_fault_ ? State::bus_fault : State::normal,
              fault ? State::bus_fault : State::normal, now_ms);
    if (fault != bus_fault_)
        bus_fault_since_ = fault ? std::optional<int64_t>(now_ms) : std::nullopt;
    bus_fault_ = fault;
    // Sort new identities so discovery permutations yield deterministic initial names.
    std::vector<uint64_t> discovered;
    for (const auto &r : readings)
        if (valid_rom(r.rom))
            discovered.push_back(r.rom);
    std::sort(discovered.begin(), discovered.end());
    discovered.erase(std::unique(discovered.begin(), discovered.end()), discovered.end());
    for (auto rom : discovered)
        if (!find(rom) && discover(rom))
            find(rom)->adopt_probe_calibration = true;
    for (auto &sensor : sensors_) {
        const auto it = std::find_if(readings.begin(), readings.end(),
                                     [&](const Reading &r) { return r.rom == sensor.rom; });
        if (it != readings.end() && it->memory) {
            // Before the reading is applied, so a restored offset already calibrates it.
            if (sensor.adopt_probe_calibration) {
                sensor.adopt_probe_calibration = false;
                const auto stored = decode_probe_calibration(*it->memory);
                if (stored && *stored != sensor.settings.calibration) {
                    sensor.settings.calibration = *stored;
                    sensor.probe_calibration = ProbeCalibration::restored;
                    ++sensor.config_revision;
                    settings_changed = true;
                }
            }
            sensor.probe_memory = it->memory;
            refresh_probe_calibration(sensor);
        }
        auto generated =
            alerts_.update(sensor, it == readings.end() ? nullptr : &*it, fault, now_ms, monitored);
        if (generated)
            record(*generated);
    }
    return settings_changed;
}
void Registry::expire(int64_t now_ms) {
    for (auto &sensor : sensors_) {
        // Only a sensor that still shows a measurement can go stale. A probe reporting errors
        // (invalid), a disconnected one, one in a bus fault and one already stale keep their state,
        // so the display copy does not flip between stale and invalid.
        const bool has_value = sensor.state == State::normal || sensor.state == State::high ||
                               sensor.state == State::low;
        if (sensor.settings.enabled && sensor.last_seen_ms &&
            now_ms - *sensor.last_seen_ms > sample_ms * 3 && has_value) {
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
bool Registry::bus_fault() const {
    return bus_fault_;
}
std::optional<int64_t> Registry::bus_fault_since() const {
    return bus_fault_since_;
}
} // namespace monitor
