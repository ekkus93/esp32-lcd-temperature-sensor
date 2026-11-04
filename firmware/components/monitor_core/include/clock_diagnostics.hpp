#pragma once

#include <cstdint>
#include <optional>

namespace monitor {
struct ClockDiagnostics {
    bool synchronized = false;
    std::optional<int64_t> last_sync_utc_seconds;
    std::optional<int64_t> sync_age_ms;
};

ClockDiagnostics make_clock_diagnostics(std::optional<int64_t> last_sync_utc_seconds,
                                        std::optional<int64_t> last_sync_monotonic_ms,
                                        int64_t now_monotonic_ms);
} // namespace monitor
