#include "storage_error_limiter.hpp"

namespace monitor {
bool StorageErrorLimiter::should_record(bool write_ok, int64_t now_ms) {
    if (write_ok) {
        failing_ = false;
        return false;
    }
    if (!failing_ || now_ms < last_recorded_ms_ || now_ms - last_recorded_ms_ >= reminder_ms) {
        failing_ = true;
        last_recorded_ms_ = now_ms;
        return true;
    }
    return false;
}
} // namespace monitor
