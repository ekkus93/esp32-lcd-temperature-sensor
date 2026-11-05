#pragma once

#include <cstdint>

namespace monitor {
struct OtaBootHealth {
    unsigned completed_cycles = 0;
    bool history_worker_started = false;
    bool wifi_stack_started = false;
    bool authentication_started = false;
    bool http_server_started = false;
    bool sampling_initialized = false;
};

// Missing probes, an unsynchronized clock, disconnected router or absent LCD
// are valid offline modes. Failed required services must not validate a new image.
inline bool ota_boot_health_ready(const OtaBootHealth &health) {
    return health.completed_cycles >= 2 && health.history_worker_started &&
           health.wifi_stack_started && health.authentication_started &&
           health.http_server_started && health.sampling_initialized;
}

// A new image that has not become healthy this long after boot rolls itself back, so a failed
// update recovers without anyone resetting the board (HTTP may be the thing that failed).
inline constexpr int64_t ota_boot_grace_ms = 120000;

enum class OtaBootAction { wait, mark_valid, roll_back };

inline OtaBootAction ota_boot_decision(const OtaBootHealth &health, int64_t ms_since_boot) {
    if (ota_boot_health_ready(health))
        return OtaBootAction::mark_valid;
    return ms_since_boot >= ota_boot_grace_ms ? OtaBootAction::roll_back : OtaBootAction::wait;
}
} // namespace monitor
