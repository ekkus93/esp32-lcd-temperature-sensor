#include "clock_diagnostics.hpp"

namespace monitor {
ClockDiagnostics make_clock_diagnostics(std::optional<int64_t> last_sync_utc_seconds,
                                        std::optional<int64_t> last_sync_monotonic_ms,
                                        int64_t now_monotonic_ms) {
    ClockDiagnostics result;
    if (!last_sync_utc_seconds || !last_sync_monotonic_ms || *last_sync_utc_seconds < 0 ||
        *last_sync_monotonic_ms < 0 || now_monotonic_ms < 0)
        return result;
    result.synchronized = true;
    result.last_sync_utc_seconds = last_sync_utc_seconds;
    result.sync_age_ms = now_monotonic_ms >= *last_sync_monotonic_ms
                             ? now_monotonic_ms - *last_sync_monotonic_ms
                             : 0;
    return result;
}
} // namespace monitor
