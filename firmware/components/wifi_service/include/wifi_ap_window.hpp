#pragma once

#include <cstdint>

namespace monitor {
// Owns independent BOOT-recovery and candidate-handoff AP-close windows.
// A candidate must not shorten a manual recovery window or close while offline.
class WifiApWindow {
  public:
    void candidate_started() {
        candidate_deadline_ms_ = 0;
        candidate_handoff_ = false;
        reconnect_pending_ = false;
    }
    void candidate_connected(int64_t now_ms) {
        candidate_deadline_ms_ = now_ms + 30000;
        candidate_handoff_ = true;
        reconnect_pending_ = false;
    }
    void station_disconnected() {
        if (candidate_handoff_) {
            candidate_deadline_ms_ = 0;
            reconnect_pending_ = true;
        }
    }
    void station_reconnected(int64_t now_ms) {
        if (candidate_handoff_ && reconnect_pending_) {
            candidate_deadline_ms_ = now_ms + 30000;
            reconnect_pending_ = false;
        }
    }
    void manual_open(int64_t now_ms) {
        // BOOT recovery must survive a concurrent candidate attempt and its
        // eventual DHCP success; it is not a 30-second handoff timer.
        manual_deadline_ms_ = now_ms + 600000;
    }
    bool should_close(int64_t now_ms, bool candidate_active, bool station_connected) {
        if (candidate_active || !station_connected || reconnect_pending_)
            return false;
        // Keep the AP until both independent grace windows have elapsed.
        int64_t deadline = candidate_deadline_ms_;
        if (manual_deadline_ms_ > deadline)
            deadline = manual_deadline_ms_;
        if (deadline == 0 || now_ms < deadline)
            return false;
        manual_deadline_ms_ = 0;
        candidate_deadline_ms_ = 0;
        candidate_handoff_ = false;
        reconnect_pending_ = false;
        return true;
    }

  private:
    int64_t manual_deadline_ms_ = 0;
    int64_t candidate_deadline_ms_ = 0;
    bool candidate_handoff_ = false;
    bool reconnect_pending_ = false;
};
} // namespace monitor
