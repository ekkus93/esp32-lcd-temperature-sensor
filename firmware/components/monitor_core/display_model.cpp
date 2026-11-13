#include "display_model.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>
namespace monitor {
namespace {
int64_t floor_div(int64_t value, int64_t divisor) {
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}
} // namespace

void bridge_short_gaps(int32_t *points, size_t count, int32_t missing, size_t max_gap) {
    size_t previous = count; // index of the last present point, or count when none yet
    for (size_t i = 0; i < count; ++i) {
        if (points[i] == missing)
            continue;
        if (previous != count && i - previous > 1 && i - previous - 1 <= max_gap) {
            const double from = points[previous], to = points[i];
            const double steps = double(i - previous);
            for (size_t k = previous + 1; k < i; ++k)
                points[k] = int32_t(std::lround(from + (to - from) * double(k - previous) / steps));
        }
        previous = i;
    }
}

TrendPoint trend_point(const Bucket &bucket) {
    TrendPoint point;
    point.boot_id = bucket.boot_id;
    point.start_ms = bucket.start_uptime_ms;
    point.utc_seconds = bucket.utc_seconds;
    for (const auto &sensor : bucket.sensors) {
        if (!sensor.count || sensor.partial || point.count == max_sensors)
            continue;
        point.roms[point.count] = sensor.rom;
        point.averages[point.count] = sensor.average;
        ++point.count;
    }
    return point;
}

TrendPoints rebase_stored_trends(const TrendPoints &stored, int64_t utc_now_ms,
                                 int64_t uptime_now_ms) {
    TrendPoints result;
    for (const auto &point : stored) {
        if (!point.utc_seconds)
            continue;
        const int64_t raw = *point.utc_seconds * 1000 - utc_now_ms + uptime_now_ms;
        if (raw > uptime_now_ms)
            continue;
        TrendPoint moved = point;
        moved.start_ms = floor_div(raw + bucket_ms / 2, bucket_ms) * bucket_ms;
        result.push_back(moved);
    }
    std::stable_sort(result.begin(), result.end(), [](const TrendPoint &a, const TrendPoint &b) {
        return a.start_ms < b.start_ms;
    });
    // Two stored windows can round onto one grid slot; keep the later one.
    TrendPoints unique;
    for (const auto &point : result) {
        if (!unique.empty() && unique.back().start_ms == point.start_ms)
            unique.back() = point;
        else
            unique.push_back(point);
    }
    return unique;
}

void TrendCache::add(const Bucket &bucket) {
    // Restored points from an earlier boot share this timeline, so the boot ID is not compared.
    // A window that does not start after the newest one is out of order: drop what is held.
    if (!points_.empty() && bucket.start_uptime_ms <= points_.back().start_ms)
        points_.clear();
    if (points_.size() == 288)
        points_.pop_front();
    points_.push_back(trend_point(bucket));
}

void TrendCache::restore(const TrendPoints &older) {
    std::deque<TrendPoint, ExternalAllocator<TrendPoint>> merged;
    for (const auto &point : older) {
        const bool before_held = points_.empty() || point.start_ms < points_.front().start_ms;
        if (before_held && (merged.empty() || point.start_ms > merged.back().start_ms))
            merged.push_back(point);
    }
    for (const auto &point : points_)
        merged.push_back(point);
    while (merged.size() > 288)
        merged.pop_front();
    points_.swap(merged);
}
std::vector<std::optional<double>> TrendCache::values(uint64_t rom, int64_t now,
                                                      unsigned minutes) const {
    if (minutes < 5 || minutes > 1440 || now < 0)
        return {};
    const size_t count = (minutes + 4) / 5;
    std::vector<std::optional<double>> result(count);
    const int64_t start = now / bucket_ms * bucket_ms - int64_t(count) * bucket_ms;
    for (const auto &point : points_) {
        const auto offset = point.start_ms - start;
        if (offset < 0 || offset >= int64_t(count) * bucket_ms)
            continue;
        for (uint8_t i = 0; i < point.count; ++i)
            if (point.roms[i] == rom)
                result[offset / bucket_ms] = point.averages[i];
    }
    return result;
}
SetupPhase setup_phase(int64_t uptime_ms) {
    constexpr int64_t phase_ms = 10000;
    const int64_t index = uptime_ms < 0 ? 0 : (uptime_ms / phase_ms) % 2;
    return index == 0 ? SetupPhase::wifi : SetupPhase::readings;
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
            // A probe in an ice-bath calibration hold (spec 17.3) raises no alarm; the LCD says so.
            row.status = sensor.calibration_hold_until_ms ? "CAL" : "";
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
