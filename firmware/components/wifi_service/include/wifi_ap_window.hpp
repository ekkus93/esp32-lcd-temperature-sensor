#pragma once

#include <cstdint>

namespace monitor {
// Owns only the provisioner's automatic AP-close timer. Failure of a new
// candidate must not inherit a shutdown from an earlier successful handoff.
class WifiApWindow {
  public:
    void candidate_started() {
        deadline_ms_ = 0;
    }
    void candidate_connected(int64_t now_ms) {
        deadline_ms_ = now_ms + 30000;
    }
    void manual_open(int64_t now_ms) {
        deadline_ms_ = now_ms + 600000;
    }
    bool should_close(int64_t now_ms, bool candidate_active) {
        if (candidate_active || deadline_ms_ == 0 || now_ms < deadline_ms_)
            return false;
        deadline_ms_ = 0;
        return true;
    }

  private:
    int64_t deadline_ms_ = 0;
};
} // namespace monitor
