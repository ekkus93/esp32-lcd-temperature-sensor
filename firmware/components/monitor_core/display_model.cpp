#include "display_model.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>
namespace monitor {
void TrendCache::add(const Bucket &bucket) {
    if (!buckets_.empty() && (bucket.boot_id != buckets_.back().boot_id ||
                              bucket.start_uptime_ms <= buckets_.back().start_uptime_ms))
        buckets_.clear(); // explicit boundary after boot/clock rebase, never join backwards
    if (buckets_.size() == 288)
        buckets_.pop_front();
    buckets_.push_back(bucket);
}
std::vector<std::optional<double>> TrendCache::values(uint64_t rom, int64_t now,
                                                      unsigned minutes) const {
    if (minutes < 5 || minutes > 1440 || now < 0)
        return {};
    const size_t count = (minutes + 4) / 5;
    std::vector<std::optional<double>> result(count);
    const int64_t start = now / bucket_ms * bucket_ms - int64_t(count) * bucket_ms;
    for (const auto &bucket : buckets_) {
        const auto offset = bucket.start_uptime_ms - start;
        if (offset < 0 || offset >= int64_t(count) * bucket_ms)
            continue;
        auto sensor = std::find_if(bucket.sensors.begin(), bucket.sensors.end(),
                                   [rom](const Aggregate &a) { return a.rom == rom; });
        if (sensor != bucket.sensors.end() && sensor->count && !sensor->partial)
            result[offset / bucket_ms] = sensor->average;
    }
    return result;
}
SetupPhase setup_phase(int64_t uptime_ms, bool compact, bool has_owner_code) {
    constexpr int64_t phase_ms = 10000;
    const bool split = compact && has_owner_code;
    const int64_t phases = split ? 3 : 2;
    const int64_t index = uptime_ms < 0 ? 0 : (uptime_ms / phase_ms) % phases;
    if (index == 0)
        return SetupPhase::wifi;
    if (split && index == 1)
        return SetupPhase::owner_code;
    return SetupPhase::readings;
}
bool valid_display_options(const DisplayOptions &o) {
    return o.overview_seconds >= 5 && o.overview_seconds <= 300 && o.trends_seconds >= 5 &&
           o.trends_seconds <= 300 && o.lookback_minutes >= 5 && o.lookback_minutes <= 1440;
}
bool expire_display_snapshot(Registry &snapshot, int64_t now_ms) {
    const auto sequence = snapshot.sequence();
    snapshot.expire(now_ms);
    return snapshot.sequence() != sequence;
}
std::array<DisplayRow, max_sensors> display_rows(const Registry &registry, bool fahrenheit) {
    std::array<DisplayRow, max_sensors> rows{};
    std::vector<const Sensor *> ordered;
    for (const auto &sensor : registry.sensors())
        ordered.push_back(&sensor);
    std::stable_sort(ordered.begin(), ordered.end(), [](const Sensor *a, const Sensor *b) {
        // Equal positions are ordered by ROM so ties never depend on discovery order.
        return std::tie(a->settings.order, a->rom) < std::tie(b->settings.order, b->rom);
    });
    for (size_t i = 0; i < rows.size(); ++i) {
        auto &row = rows[i];
        row.label = "Unused";
        row.value = "--.-";
        row.status = "NO SENSOR";
        if (i >= ordered.size())
            continue;
        const auto &sensor = *ordered[i];
        row.rom = sensor.rom;
        row.state = sensor.state;
        row.label =
            sensor.settings.short_name.empty() ? sensor.settings.name : sensor.settings.short_name;
        switch (row.state) {
        case State::normal:
            row.status = "";
            break;
        case State::high:
            row.status = "! HIGH";
            break;
        case State::low:
            row.status = "! LOW";
            break;
        case State::disabled:
            row.status = "OFF";
            row.value = "OFF";
            break;
        case State::disconnected:
            row.status = "! LOST";
            break;
        case State::stale:
            row.status = "STALE";
            break;
        case State::invalid:
            row.status = "ERR";
            break;
        case State::bus_fault:
            row.status = "BUS FAULT";
            break;
        case State::unassigned:
            row.status = "NO SENSOR";
            break;
        }
        if ((row.state == State::normal || row.state == State::high || row.state == State::low) &&
            sensor.temperature && std::isfinite(*sensor.temperature)) {
            const double celsius = *sensor.temperature, degrees_f = celsius * 1.8 + 32;
            char text[24];
            std::snprintf(text, sizeof(text), "%.1f\xC2\xB0%s", fahrenheit ? degrees_f : celsius,
                          fahrenheit ? "F" : "C");
            row.value = text;
            std::snprintf(text, sizeof(text), "%.1f\xC2\xB0%s", fahrenheit ? celsius : degrees_f,
                          fahrenheit ? "C" : "F");
            row.alt_value = text;
        }
    }
    return rows;
}
} // namespace monitor
