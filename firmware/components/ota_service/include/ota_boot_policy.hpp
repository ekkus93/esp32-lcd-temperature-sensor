#pragma once

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
} // namespace monitor
