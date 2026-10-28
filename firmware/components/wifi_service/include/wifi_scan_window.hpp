#pragma once

#include <cstdint>

namespace monitor {
// Track only scans actually accepted by the Wi-Fi driver. A failed/denied
// scan must never leave a timer that can later interrupt STA association.
class WifiScanWindow {
  public:
    void started(int64_t now_ms, bool accepted) {
        deadline_ms_ = accepted ? now_ms + 10000 : 0;
    }
    void cancel() {
        deadline_ms_ = 0;
    }
    bool active() const {
        return deadline_ms_ != 0;
    }
    enum class Completion { ignored, accepted, timed_out };
    Completion completed(int64_t now_ms) {
        // A late completion must release scan ownership even when the worker
        // has not yet run its periodic timeout check.
        if (!active())
            return Completion::ignored;
        const bool timely = now_ms < deadline_ms_;
        cancel();
        return timely ? Completion::accepted : Completion::timed_out;
    }
    bool expired(int64_t now_ms) {
        if (!active() || now_ms < deadline_ms_)
            return false;
        cancel();
        return true;
    }

  private:
    int64_t deadline_ms_ = 0;
};
} // namespace monitor
