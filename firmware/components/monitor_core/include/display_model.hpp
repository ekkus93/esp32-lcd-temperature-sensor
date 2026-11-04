#pragma once
#include "monitor.hpp"
#include <array>
#include <deque>
namespace monitor {
class TrendCache {
  public:
    void add(const Bucket &bucket);
    std::vector<std::optional<double>> values(uint64_t rom, int64_t now_ms, unsigned minutes) const;
    size_t size() const {
        return buckets_.size();
    }

  private:
    std::deque<Bucket> buckets_; // <= 24 hours of five-minute windows, RAM only
};
struct DisplayOptions {
    bool fahrenheit = true; // default scale; saved TMD1 settings keep their own value
    bool extended = false;
    unsigned overview_seconds = 20, trends_seconds = 10, lookback_minutes = 60;
};
struct DisplayStatus {
    std::string wifi = "offline";
    std::string local_time; // empty unless time is trusted and converted by clock service
    bool logging = false;
    unsigned button_progress = 0;
    std::string ota_state; // pending verification or rollback status, not a new LCD view
};
struct DisplayRow {
    uint64_t rom = 0;
    // `value` is in the configured unit; `alt_value` is the same reading in the other unit and
    // is empty unless the row has a numeric reading.
    std::string label, value, alt_value, status;
    State state = State::unassigned;
};
std::array<DisplayRow, max_sensors> display_rows(const Registry &registry, bool fahrenheit);
bool valid_display_options(const DisplayOptions &options);
bool expire_display_snapshot(Registry &snapshot, int64_t now_ms);
} // namespace monitor
