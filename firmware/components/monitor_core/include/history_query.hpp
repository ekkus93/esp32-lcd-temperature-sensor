#pragma once
#include "history.hpp"
#include <string_view>
namespace monitor {
struct HistoryQuery {
    // Unknown-clock records are selected by boot/uptime, never mixed across boots.
    std::optional<uint64_t> boot_id;
    int64_t from = 0, to = 0;  // milliseconds: UTC if boot_id is absent, else uptime
    unsigned max_points = 120; // number of five-sensor time groups, at most 512
    std::vector<uint64_t> roms;
};
struct HistoryPoint {
    uint64_t boot_id, rom;
    int64_t start_ms, end_ms;
    std::optional<int64_t> utc_seconds;
    double average, minimum, maximum;
    uint64_t count;
    bool partial;
};
struct HistoryGap {
    uint64_t boot_id, rom;
    int64_t start_ms, end_ms;
};
bool valid_query(const HistoryQuery &query);
// Parse the bounded, already URL-decoded query string used by history endpoints.
std::optional<HistoryQuery> parse_history_query(std::string_view text,
                                                bool *has_max_points = nullptr);
std::vector<HistoryGap> history_gaps(const std::vector<HistoryPoint> &points);
// Bounded reducer. Empty intervals remain absent; a gap makes its group partial.
class HistoryReducer {
  public:
    explicit HistoryReducer(HistoryQuery query);
    void add(const Bucket &bucket);
    std::vector<HistoryPoint> finish() const;
    bool truncated() const {
        return truncated_;
    }

  private:
    struct Cell {
        HistoryPoint point;
        int64_t last_ms;
    };
    HistoryQuery query_;
    int64_t width_ = bucket_ms;
    std::vector<std::vector<Cell>> groups_;
    bool truncated_ = false;
};
} // namespace monitor
