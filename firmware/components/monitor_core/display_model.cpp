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

std::vector<Bucket> rebase_stored_buckets(const std::vector<Bucket> &stored, int64_t utc_now_ms,
                                          int64_t uptime_now_ms) {
    std::vector<Bucket> result;
    for (const auto &bucket : stored) {
        if (!bucket.utc_seconds)
            continue;
        const int64_t raw = *bucket.utc_seconds * 1000 - utc_now_ms + uptime_now_ms;
        if (raw > uptime_now_ms)
            continue;
        Bucket moved = bucket;
        moved.start_uptime_ms = floor_div(raw + bucket_ms / 2, bucket_ms) * bucket_ms;
        result.push_back(std::move(moved));
    }
    std::stable_sort(result.begin(), result.end(), [](const Bucket &a, const Bucket &b) {
        return a.start_uptime_ms < b.start_uptime_ms;
    });
    // Two stored windows can round onto one grid slot; keep the later one.
    std::vector<Bucket> unique;
    for (auto &bucket : result) {
        if (!unique.empty() && unique.back().start_uptime_ms == bucket.start_uptime_ms)
            unique.back() = std::move(bucket);
        else
            unique.push_back(std::move(bucket));
    }
    return unique;
}

void TrendCache::add(const Bucket &bucket) {
    // Restored buckets from an earlier boot share this timeline, so the boot ID is not compared.
    // A window that does not start after the newest one is out of order: drop what is held.
    if (!buckets_.empty() && bucket.start_uptime_ms <= buckets_.back().start_uptime_ms)
        buckets_.clear();
    if (buckets_.size() == 288)
        buckets_.pop_front();
    buckets_.push_back(bucket);
}

void TrendCache::restore(const std::vector<Bucket> &older) {
    std::deque<Bucket> merged;
    for (const auto &bucket : older) {
        const bool before_held =
            buckets_.empty() || bucket.start_uptime_ms < buckets_.front().start_uptime_ms;
        if (before_held &&
            (merged.empty() || bucket.start_uptime_ms > merged.back().start_uptime_ms))
            merged.push_back(bucket);
    }
    for (const auto &bucket : buckets_)
        merged.push_back(bucket);
    while (merged.size() > 288)
        merged.pop_front();
    buckets_.swap(merged);
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
