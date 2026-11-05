#pragma once
#include <array>
#include <cstdint>

namespace monitor {
enum class PublicRequest { history, export_csv };

// History is public, so a visitor (or a script) must not be able to keep the device busy:
//  - each client may start one history query per second and one CSV export per 30 seconds;
//  - at most two history-type requests (queries or exports) run at once, from anyone.
// A refused request costs the client nothing. Clients are told how long to wait.
class PublicLimiter {
  public:
    static constexpr int64_t history_interval_ms = 1000;
    static constexpr int64_t export_interval_ms = 30000;
    static constexpr unsigned max_concurrent = 2;
    struct Decision {
        bool allow;
        int64_t retry_after_ms; // meaningful when `allow` is false
    };
    // On `allow` the request counts as running until finish().
    Decision begin(PublicRequest kind, uint32_t client, int64_t now_ms);
    void finish(PublicRequest kind);

  private:
    struct Client {
        uint32_t id = 0;
        bool used = false;
        int64_t last_history_ms = 0;
        int64_t last_export_ms = 0;
        bool had_history = false;
        bool had_export = false;
        int64_t last_used_ms = 0;
    };
    std::array<Client, 8> clients_{};
    unsigned running_ = 0;
};
} // namespace monitor
