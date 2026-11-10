#pragma once
#include "monitor.hpp"
#include <array>
#include <deque>
namespace monitor {
// Chart points: a run of at most `max_gap` missing points that has a value on both sides is drawn
// as a straight line between them (sampling jitter, one or two skipped windows). Longer runs, such
// as a reboot, stay missing so the chart never shows readings that were not taken. Interpolated
// values lie between their neighbours, so they do not change the scale.
void bridge_short_gaps(int32_t *points, size_t count, int32_t missing, size_t max_gap);

// Stored buckets are placed on the current boot's uptime timeline, so the trends can show the last
// hour right after a restart. Each bucket needs its UTC start (buckets without one are dropped, as
// their position cannot be known); the start is rounded to the five-minute grid that
// TrendCache::values uses. Buckets that would start after `uptime_now_ms` are dropped. The result
// is oldest first.
std::vector<Bucket> rebase_stored_buckets(const std::vector<Bucket> &stored, int64_t utc_now_ms,
                                          int64_t uptime_now_ms);

class TrendCache {
  public:
    void add(const Bucket &bucket);
    // Older buckets (oldest first, on this boot's timeline) go in front of the held ones; anything
    // that does not start before the oldest held bucket is skipped.
    void restore(const std::vector<Bucket> &older);
    std::vector<std::optional<double>> values(uint64_t rom, int64_t now_ms, unsigned minutes) const;
    size_t size() const {
        return buckets_.size();
    }

  private:
    std::deque<Bucket> buckets_; // <= 24 hours of five-minute windows, RAM only, oldest first
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
// While setup mode is open the LCD rotates, ten seconds each, between the setup page and the
// normal readings, so temperatures and alerts stay visible.
enum class SetupPhase { wifi, readings };
SetupPhase setup_phase(int64_t uptime_ms);
bool valid_display_options(const DisplayOptions &options);
bool expire_display_snapshot(Registry &snapshot, int64_t now_ms);
} // namespace monitor
