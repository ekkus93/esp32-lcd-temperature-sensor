#pragma once
#include "monitor.hpp"
#include <functional>
namespace monitor {
constexpr size_t max_history_record_bytes = 335;
// All integer fields are encoded little-endian; no native struct padding is persisted.
std::vector<uint8_t> encode_bucket(const Bucket &bucket);
std::optional<Bucket> decode_bucket(const std::vector<uint8_t> &bytes);
// The time window a reader wants: UTC milliseconds when `boot_id` is unset, otherwise uptime
// milliseconds of that boot. `from` is inclusive and `to` exclusive.
struct HistoryRange {
    std::optional<uint64_t> boot_id;
    int64_t from = 0, to = 0;
};
// Whether a record's start time falls in the window. A UTC window never matches a record without a
// trusted UTC time.
bool history_bucket_in_range(const Bucket &bucket, const HistoryRange &range);
// File-identity checks. The store can be pointed at an ordinary host directory (the host tests do),
// where another process could swap or replace a segment file between two operations. Behind
// `#ifndef ESP_PLATFORM` it therefore also remembers each file's device and inode (and the
// directory's, and the active segment's) and refuses to continue when they change. These checks are
// compiled for the host only: the device build does not include them, and there the same file is
// protected by the size and content-fingerprint checks that run in both builds (`segment_has_size`,
// `unchanged_segment_contents`), which is enough because the firmware is the only user of the
// history partition. Anything that matters for the device must therefore be covered by the
// size/fingerprint checks, not by the identity ones.
class HistoryStore {
  public:
    explicit HistoryStore(std::string directory, std::size_t max_bytes = 3 * 1024 * 1024,
                          std::size_t segment_bytes = 32 * 1024);
    bool recover();
    bool append(const Bucket &bucket);
    // Completed segments that fail validation are skipped, not fatal; `skipped` (optional)
    // receives how many this visit passed over. A damaged active segment still fails the visit.
    // With a `range`, segments that recovery or an append indexed as holding no record in that
    // window are not read at all, so a short query costs the segments it covers rather than the
    // whole store. Those unread segments are also not revalidated by this visit; they were checked
    // when recovered and are fingerprinted before eviction. The visitor can still receive records
    // outside the window (from a segment that also holds matching ones) and must filter them.
    bool visit(const std::function<bool(const Bucket &)> &visitor, std::size_t *skipped = nullptr,
               const HistoryRange *range = nullptr) const;
    // Damaged completed segments found by the last recover(); they are kept on disk untouched.
    std::size_t skipped_segments() const {
        return skipped_segments_;
    }
    std::size_t bytes() const;

  private:
    std::string directory_;
    std::size_t max_bytes_;
    std::size_t segment_bytes_;
    unsigned active_ = 0;
    // No writes before first successful recovery or after a failed append/recovery.
    bool append_ready_ = false;
    // The last append failed on I/O (not on an integrity check), so the next one may try again.
    bool retry_after_error_ = false;
    std::size_t skipped_segments_ = 0;
    // Detect deletion of the active segment after a successful recovery/write.
    bool active_segment_exists_ = false;
    // Recovered/committed byte count of the active segment (including rollover).
    std::size_t active_size_ = 0;
    // Fingerprint of the recovered/committed active bytes. Accidental-change guard.
    uint64_t active_fingerprint_ = 0;
    // Which times a segment holds, so a ranged visit can leave it unread. Built from the records
    // recovery parsed and extended by each append. `known` is false when the segment could not be
    // indexed (a damaged completed segment); such a segment is always read.
    struct SegmentSpan {
        bool known = true;
        bool has_utc = false;
        int64_t utc_first_ms = 0, utc_last_ms = 0;
        struct Boot {
            uint64_t id;
            int64_t first_ms, last_ms;
        };
        std::vector<Boot> boots;
        void add(const Bucket &bucket);
        bool may_hold(const HistoryRange &range) const;
    };
    // Seal each retained segment at recovery/append. Only eviction candidates
    // need an expensive content recheck on the regular five-minute write path.
    struct SegmentSeal {
        unsigned number;
        std::size_t size;
        uint64_t fingerprint;
        SegmentSpan span{};
#ifndef ESP_PLATFORM
        uint64_t device = 0;
        uint64_t inode = 0;
#endif
    };
    std::vector<SegmentSeal> sealed_segments_;
#ifndef ESP_PLATFORM
    // Directory identity observed at the most recent successful recovery.
    uint64_t directory_dev_ = 0;
    uint64_t directory_ino_ = 0;
    // The active segment must still be the file validated by recover()/append().
    uint64_t active_dev_ = 0;
    uint64_t active_ino_ = 0;
#endif
    std::vector<unsigned> segments() const;
    std::string path(unsigned number) const;
};
} // namespace monitor
