#include "history_query.hpp"
#include <algorithm>
namespace monitor {
bool valid_query(const HistoryQuery &q) {
    return q.from >= 0 && q.to > q.from && q.to - q.from <= 31LL * 86400000 && q.max_points >= 1 &&
           q.max_points <= 512 && q.roms.size() <= max_sensors &&
           std::all_of(q.roms.begin(), q.roms.end(), valid_rom);
}
HistoryReducer::HistoryReducer(HistoryQuery query) : query_(std::move(query)) {
    if (!valid_query(query_))
        return;
    const auto windows = (query_.to - query_.from + bucket_ms - 1) / bucket_ms;
    width_ = ((windows + query_.max_points - 1) / query_.max_points) * bucket_ms;
    groups_.resize((query_.to - query_.from + width_ - 1) / width_);
}
void HistoryReducer::add(const Bucket &bucket) {
    if (groups_.empty())
        return;
    int64_t time = bucket.start_uptime_ms;
    if (query_.boot_id) {
        if (bucket.boot_id != *query_.boot_id)
            return;
    } else {
        if (!bucket.utc_seconds || *bucket.utc_seconds < 0 ||
            *bucket.utc_seconds > INT64_MAX / 1000)
            return;
        time = *bucket.utc_seconds * 1000;
    }
    if (time < query_.from || time >= query_.to)
        return;
    auto &group = groups_[(time - query_.from) / width_];
    for (const auto &a : bucket.sensors) {
        if (!a.count || (!query_.roms.empty() && std::find(query_.roms.begin(), query_.roms.end(),
                                                           a.rom) == query_.roms.end()))
            continue;
        // A trusted-clock reboot must still retain an explicit series boundary.
        auto found = std::find_if(group.begin(), group.end(), [&](const Cell &c) {
            return c.point.rom == a.rom && c.point.boot_id == bucket.boot_id;
        });
        if (found == group.end()) {
            // At most five cells per time group: bound memory even after many reboots.
            // Mixed-boot overflow is represented by missing groups, not invented values.
            if (group.size() == max_sensors) {
                truncated_ = true;
                continue;
            }
            group.push_back({{bucket.boot_id, a.rom, time, time + bucket_ms, bucket.utc_seconds,
                              a.average, a.minimum, a.maximum, a.count, a.partial},
                             time});
        } else {
            auto &p = found->point;
            // Never merge overlapping or reversed records after an NTP step.
            if (time <= found->last_ms) {
                p.partial = true;
                truncated_ = true;
                continue;
            }
            p.average = (p.average * p.count + a.average * a.count) / (p.count + a.count);
            p.minimum = std::min(p.minimum, a.minimum);
            p.maximum = std::max(p.maximum, a.maximum);
            p.count += a.count;
            p.partial = p.partial || a.partial || time != found->last_ms + bucket_ms;
            p.end_ms = time + bucket_ms;
            found->last_ms = time;
        }
    }
}
std::vector<HistoryPoint> HistoryReducer::finish() const {
    std::vector<HistoryPoint> output;
    for (const auto &group : groups_)
        for (const auto &cell : group)
            output.push_back(cell.point);
    return output;
}
} // namespace monitor
