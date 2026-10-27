#include "monitor.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace monitor {
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
            const auto aligned_utc = (*utc / 300) * 300;
            if (!next_utc_boundary_ || aligned_utc >= *next_utc_boundary_) {
                start_ = now_ms;
                utc_ = aligned_utc;
                next_utc_boundary_ = aligned_utc + 300;
                const auto duration = bucket_ms - (*utc % 300) * 1000;
                deadline_ = now_ms + duration;
                partial_ = duration != bucket_ms;
            } else {
                // A backward step or forward step within the same UTC window
                // would otherwise give two records the same five-minute key.
                // Keep this interval boot-relative until wall time reaches the
                // next unoccupied trusted boundary.
                start_ = now_ms;
                utc_.reset();
                deadline_ = now_ms + bucket_ms;
                partial_ = true;
            }
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
