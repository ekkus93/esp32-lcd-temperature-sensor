#pragma once
#include <cstdint>

namespace monitor {
// Decides which failed history writes are journaled as `storage_error`: the first failure after
// a healthy write, then one reminder per hour while the fault persists. A failing flash would
// otherwise write one journal event per five-minute bucket and push alarm history out of the
// journal.
class StorageErrorLimiter {
  public:
    static constexpr int64_t reminder_ms = 3600 * 1000;
    // Reports the outcome of one write; returns true when an event should be journaled.
    bool should_record(bool write_ok, int64_t now_ms);

  private:
    bool failing_ = false;
    int64_t last_recorded_ms_ = 0;
};
} // namespace monitor
