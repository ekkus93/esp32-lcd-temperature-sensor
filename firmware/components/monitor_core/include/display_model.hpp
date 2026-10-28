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
    bool fahrenheit = false;
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
    std::string label, value, status;
    State state = State::unassigned;
};
std::array<DisplayRow, max_sensors> display_rows(const Registry &registry, bool fahrenheit);
bool valid_display_options(const DisplayOptions &options);
} // namespace monitor
