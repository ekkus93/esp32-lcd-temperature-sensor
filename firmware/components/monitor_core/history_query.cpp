#include "history_query.hpp"
#include <algorithm>
#include <charconv>
namespace monitor {
std::optional<HistoryQuery> parse_history_query(std::string_view text, bool *has_max_points) {
    if (text.empty() || text.size() > 256)
        return std::nullopt;
    HistoryQuery query;
    std::vector<std::string_view> keys;
    bool saw_max_points = false;
    for (size_t start = 0; start < text.size();) {
        auto end = text.find('&', start);
        if (end == std::string_view::npos)
            end = text.size();
        auto item = text.substr(start, end - start);
        auto equals = item.find('=');
        if (equals == std::string_view::npos || equals == 0 || equals + 1 == item.size())
            return std::nullopt;
        auto key = item.substr(0, equals);
        auto value = item.substr(equals + 1);
        if (key != "rom_id" && std::find(keys.begin(), keys.end(), key) != keys.end())
            return std::nullopt;
        keys.push_back(key);
        uint64_t number = 0;
        const int base = key == "boot_id" || key == "rom_id" ? 16 : 10;
        auto parsed = std::from_chars(value.data(), value.data() + value.size(), number, base);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
            return std::nullopt;
        if (key == "boot_id" && value.size() == 16)
            query.boot_id = number;
        else if ((key == "from" || key == "to") && number <= INT64_MAX) {
            if (key == "from")
                query.from = int64_t(number);
            else
                query.to = int64_t(number);
        } else if (key == "max_points" && number >= 1 && number <= 512) {
            query.max_points = unsigned(number);
            saw_max_points = true;
        } else if (key == "rom_id" && value.size() == 16) {
            uint64_t rom = 0;
            for (unsigned i = 0; i < 8; ++i)
                rom |= ((number >> (8 * i)) & 0xff) << (8 * (7 - i));
            query.roms.push_back(rom);
        } else
            return std::nullopt;
        start = end + 1;
    }
    if (!valid_query(query))
        return std::nullopt;
    if (has_max_points)
        *has_max_points = saw_max_points;
    return query;
}
std::vector<HistoryGap> history_gaps(const std::vector<HistoryPoint> &points) {
    auto ordered = points;
    std::sort(ordered.begin(), ordered.end(), [](const auto &left, const auto &right) {
        if (left.boot_id != right.boot_id)
            return left.boot_id < right.boot_id;
        if (left.rom != right.rom)
            return left.rom < right.rom;
        return left.start_ms < right.start_ms;
    });
    std::vector<HistoryGap> gaps;
    for (size_t i = 1; i < ordered.size(); ++i) {
        const auto &previous = ordered[i - 1];
        const auto &current = ordered[i];
        if (previous.boot_id == current.boot_id && previous.rom == current.rom &&
            current.start_ms > previous.end_ms)
            gaps.push_back({current.boot_id, current.rom, previous.end_ms, current.start_ms});
    }
    return gaps;
}
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
