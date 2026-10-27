#include "monitor.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace monitor {
const char *state_name(State state) {
    static constexpr const char *names[] = {"normal",     "high",     "low",
                                            "unassigned", "disabled", "disconnected",
                                            "stale",      "invalid",  "bus_fault"};
    return names[static_cast<unsigned>(state)];
}
uint8_t crc8(const uint8_t *data, std::size_t length) {
    uint8_t crc = 0;
    for (std::size_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            bool mix = (crc ^ byte) & 1;
            crc >>= 1;
            if (mix)
                crc ^= 0x8c;
            byte >>= 1;
        }
    }
    return crc;
}
bool valid_rom(uint64_t rom) {
    std::array<uint8_t, 8> bytes{};
    for (unsigned i = 0; i < 8; ++i)
        bytes[i] = (rom >> (i * 8)) & 0xff;
    return bytes[0] == 0x28 && crc8(bytes.data(), 7) == bytes[7];
}
std::string rom_hex(uint64_t rom) {
    char out[17]{};
    for (unsigned i = 0; i < 8; ++i)
        std::snprintf(out + i * 2, 3, "%02x", unsigned((rom >> (i * 8)) & 0xff));
    return out;
}
std::optional<double> decode_temperature(const std::array<uint8_t, 9> &data, bool complete) {
    if (!complete || crc8(data.data(), 8) != data[8])
        return std::nullopt;
    int16_t raw = static_cast<int16_t>((uint16_t(data[1]) << 8) | data[0]);
    double value = raw / 16.0;
    if (value < -55 || value > 125)
        return std::nullopt;
    return value;
}
namespace {
bool label_text(const std::string &text) {
    for (std::size_t i = 0; i < text.size();) {
        unsigned char lead = text[i++];
        if (lead < 0x20 || lead == 0x7f)
            return false;
        if (lead < 0x80)
            continue;
        unsigned remaining = 0;
        uint32_t code = 0;
        uint32_t minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) {
            remaining = 1;
            code = lead & 0x1f;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            remaining = 2;
            code = lead & 0x0f;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            remaining = 3;
            code = lead & 7;
            minimum = 0x10000;
        } else
            return false;
        if (i + remaining > text.size())
            return false;
        for (unsigned j = 0; j < remaining; ++j) {
            unsigned char next = text[i++];
            if ((next & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return false;
    }
    return true;
}
} // namespace
bool valid_settings(const Settings &s) {
    return !s.name.empty() && s.name.size() <= 32 && s.short_name.size() <= 12 &&
           label_text(s.name) && label_text(s.short_name) && s.order < max_sensors &&
           std::isfinite(s.calibration) && std::abs(s.calibration) <= 20 &&
           std::isfinite(s.hysteresis) && s.hysteresis >= 0 && s.hysteresis <= 20 &&
           s.persistence_ms >= 0 && s.persistence_ms <= 3600000 &&
           (!s.low || (std::isfinite(*s.low) && *s.low >= -55 && *s.low <= 125)) &&
           (!s.high || (std::isfinite(*s.high) && *s.high >= -55 && *s.high <= 125)) &&
           (!s.low || !s.high || *s.low < *s.high);
}
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
    if (events_.size() == 128)
        events_.erase(events_.begin());
    events_.push_back({++event_sequence_, rom, previous, state, now_ms});
}
void Registry::transition(Sensor &sensor, State state, int64_t now_ms) {
    if (sensor.state == state)
        return;
    auto alert = [](State s) {
        return s == State::high || s == State::low || s == State::disconnected;
    };
    if ((alert(sensor.state) || alert(state)) &&
        (state != State::disconnected || sensor.settings.disconnect_alert)) {
        event(sensor.rom, sensor.state, state, now_ms);
    }
    sensor.state = state;
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
        if (!sensor.settings.enabled) {
            sensor.state = State::disabled;
            sensor.temperature.reset();
            continue;
        }
        if (fault) {
            sensor.state = State::bus_fault;
            sensor.temperature.reset();
            sensor.pending_since.reset();
            continue;
        }
        const auto it = std::find_if(readings.begin(), readings.end(),
                                     [&](const Reading &r) { return r.rom == sensor.rom; });
        bool valid = it != readings.end() && it->conversion_complete && it->celsius &&
                     std::isfinite(*it->celsius) && *it->celsius >= -55 && *it->celsius <= 125;
        if (!valid) {
            sensor.temperature.reset();
            sensor.pending_since.reset();
            if (it != readings.end())
                sensor.failures = 0;
            else if (sensor.failures < 3)
                ++sensor.failures;
            transition(sensor, sensor.failures >= 3 ? State::disconnected : State::invalid, now_ms);
            continue;
        }
        sensor.failures = 0;
        sensor.last_seen_ms = now_ms;
        sensor.temperature = *it->celsius + sensor.settings.calibration;
        double value = *sensor.temperature;
        State target = State::normal;
        if (sensor.settings.high && (value > *sensor.settings.high ||
                                     (sensor.state == State::high &&
                                      value > *sensor.settings.high - sensor.settings.hysteresis)))
            target = State::high;
        else if (sensor.settings.low &&
                 (value < *sensor.settings.low ||
                  (sensor.state == State::low &&
                   value < *sensor.settings.low + sensor.settings.hysteresis)))
            target = State::low;
        if (target == State::normal || target == sensor.state) {
            sensor.pending_since.reset();
            transition(sensor, target, now_ms);
        } else {
            if (!sensor.pending_since || sensor.pending != target) {
                sensor.pending = target;
                sensor.pending_since = now_ms;
            }
            if (now_ms - *sensor.pending_since >= sensor.settings.persistence_ms)
                transition(sensor, target, now_ms);
            else if (sensor.state != State::high && sensor.state != State::low)
                transition(sensor, State::normal, now_ms);
        }
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
Aggregator::Aggregator(uint64_t boot_id) : boot_id_(boot_id) {}
std::optional<Bucket> Aggregator::add(const Registry &registry, int64_t now_ms,
                                      std::optional<int64_t> utc) {
    if (last_sample_ && now_ms <= *last_sample_)
        return std::nullopt;
    std::optional<Bucket> finished;
    // Split on time-quality changes and clock jumps as well as normal boundaries.
    bool clock_change =
        start_ && (utc.has_value() != anchor_utc_.has_value() ||
                   (utc && anchor_utc_ &&
                    std::abs((*utc - *anchor_utc_) * 1000 - (now_ms - anchor_ms_)) > 2000));
    if (start_ && (now_ms >= deadline_ || clock_change)) {
        finished = Bucket{boot_id_, *start_, utc_, {}};
        for (const auto &a : accumulators_)
            finished->sensors.push_back({a.rom, a.count ? a.sum / a.count : 0, a.minimum, a.maximum,
                                         a.count, partial_ || clock_change || a.count < 60, a.name,
                                         a.state_flags});
        start_.reset();
        accumulators_.clear();
    }
    if (!start_) {
        anchor_ms_ = now_ms;
        anchor_utc_ = utc;
        if (utc) {
            start_ = now_ms;
            utc_ = (*utc / 300) * 300;
            const auto duration = bucket_ms - (*utc % 300) * 1000;
            deadline_ = now_ms + duration;
            partial_ = duration != bucket_ms;
        } else {
            start_ = (now_ms / bucket_ms) * bucket_ms;
            utc_.reset();
            deadline_ = *start_ + bucket_ms;
            partial_ = false;
        }
    }
    last_sample_ = now_ms;
    for (const auto &sensor : registry.sensors()) {
        auto it = std::find_if(accumulators_.begin(), accumulators_.end(),
                               [&](const Accumulator &a) { return a.rom == sensor.rom; });
        if (it == accumulators_.end()) {
            accumulators_.push_back({sensor.rom});
            it = accumulators_.end() - 1;
        }
        it->name = sensor.settings.name;
        it->state_flags |= uint16_t(1u << unsigned(sensor.state));
        if (!sensor.temperature || !sensor.settings.enabled || !sensor.last_seen_ms ||
            *sensor.last_seen_ms != now_ms)
            continue;
        double value = *sensor.temperature;
        if (!it->count)
            it->minimum = it->maximum = value;
        it->sum += value;
        it->minimum = std::min(it->minimum, value);
        it->maximum = std::max(it->maximum, value);
        ++it->count;
    }
    return finished;
}
void Clock::synchronize(int64_t monotonic_ms, int64_t utc_seconds) {
    origin_ = monotonic_ms;
    utc_ = utc_seconds;
}
std::optional<int64_t> Clock::utc(int64_t monotonic_ms) const {
    if (!origin_ || monotonic_ms < *origin_)
        return std::nullopt;
    return utc_ + (monotonic_ms - *origin_) / 1000;
}
bool BootButton::update(bool pressed, int64_t now_ms) {
    if (pressed != raw_) {
        raw_ = pressed;
        changed_ = now_ms;
    }
    if (now_ms - changed_ >= 30 && stable_ != raw_) {
        stable_ = raw_;
        if (stable_) {
            pressed_ = now_ms;
            fired_ = false;
        } else {
            armed_ = true;
            fired_ = false;
        }
    }
    if (!raw_ && now_ms - changed_ >= 30)
        armed_ = true;
    if (armed_ && raw_ && stable_ && !fired_ && now_ms - pressed_ >= 5000) {
        fired_ = true;
        return true;
    }
    return false;
}
unsigned BootButton::progress(int64_t now_ms) const {
    if (!armed_ || !raw_ || !stable_)
        return 0;
    return static_cast<unsigned>(
        std::min<int64_t>(100, std::max<int64_t>(0, now_ms - pressed_) / 50));
}
bool DisplayCycle::configure(bool extended, unsigned overview_s, unsigned trends_s,
                             int64_t now_ms) {
    if (overview_s < 5 || overview_s > 300 || trends_s < 5 || trends_s > 300)
        return false;
    extended_ = extended;
    overview_s_ = overview_s;
    trends_s_ = trends_s;
    start_ = now_ms;
    return true;
}
bool DisplayCycle::trends(int64_t now_ms) const {
    if (!extended_ || now_ms < start_)
        return false;
    return ((now_ms - start_) / 1000) % (overview_s_ + trends_s_) >= overview_s_;
}
} // namespace monitor
