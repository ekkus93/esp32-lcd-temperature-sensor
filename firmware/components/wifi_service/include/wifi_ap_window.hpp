#pragma once

#include <cstdint>

namespace monitor {
// Owns only the provisioner's automatic AP-close timer. A candidate handoff
// must not close the setup AP after the station loses its IP address.
class WifiApWindow {
  public:
    void candidate_started() {
        deadline_ms_ = 0;
        candidate_handoff_ = false;
        reconnect_pending_ = false;
    }
    void candidate_connected(int64_t now_ms) {
        deadline_ms_ = now_ms + 30000;
        candidate_handoff_ = true;
        reconnect_pending_ = false;
    }
    void station_disconnected() {
        if (candidate_handoff_) {
            deadline_ms_ = 0;
            reconnect_pending_ = true;
        }
    }
    void station_reconnected(int64_t now_ms) {
        if (candidate_handoff_ && reconnect_pending_) {
            deadline_ms_ = now_ms + 30000;
            reconnect_pending_ = false;
        }
    }
    void manual_open(int64_t now_ms) {
        deadline_ms_ = now_ms + 600000;
        candidate_handoff_ = false;
        reconnect_pending_ = false;
    }
    bool should_close(int64_t now_ms, bool candidate_active, bool station_connected) {
        if (candidate_active || !station_connected || deadline_ms_ == 0 || now_ms < deadline_ms_)
            return false;
        deadline_ms_ = 0;
        candidate_handoff_ = false;
        reconnect_pending_ = false;
        return true;
    }

  private:
    int64_t deadline_ms_ = 0;
    bool candidate_handoff_ = false;
    bool reconnect_pending_ = false;
};
} // namespace monitor
