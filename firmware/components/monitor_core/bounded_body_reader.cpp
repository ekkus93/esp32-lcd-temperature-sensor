#include "bounded_body_reader.hpp"
#include <algorithm>
namespace monitor {
BodyReadStatus read_exact_body(size_t length, size_t limit, int64_t deadline_ms, void *context,
                               BodyReceive receive, BodyClock clock, std::string &body) {
    body.clear();
    if (length > limit)
        return BodyReadStatus::too_large;
    if (!receive || !clock || length == 0)
        return BodyReadStatus::receive_error;
    body.assign(length, '\0');
    size_t used = 0;
    while (used < length) {
        if (clock(context) >= deadline_ms) {
            body.clear();
            return BodyReadStatus::timeout;
        }
        const int count = receive(context, body.data() + used, length - used);
        const auto received_at = clock(context);
        if (count <= 0) {
            body.clear();
            if (received_at >= deadline_ms)
                return BodyReadStatus::timeout;
            return count == 0 ? BodyReadStatus::eof : BodyReadStatus::receive_error;
        }
        if (received_at >= deadline_ms || static_cast<size_t>(count) > length - used) {
            body.clear();
            return received_at >= deadline_ms ? BodyReadStatus::timeout
                                              : BodyReadStatus::receive_error;
        }
        used += static_cast<size_t>(count);
    }
    return BodyReadStatus::complete;
}
BodyReadStatus discard_exact_body(size_t length, int64_t deadline_ms, void *context,
                                  BodyReceive receive, BodyClock clock) {
    if (!receive || !clock || length == 0)
        return BodyReadStatus::receive_error;
    char scratch[256];
    size_t used = 0;
    while (used < length) {
        if (clock(context) >= deadline_ms)
            return BodyReadStatus::timeout;
        const auto request = std::min(sizeof(scratch), length - used);
        const int count = receive(context, scratch, request);
        const auto received_at = clock(context);
        if (count <= 0) {
            if (received_at >= deadline_ms)
                return BodyReadStatus::timeout;
            return count == 0 ? BodyReadStatus::eof : BodyReadStatus::receive_error;
        }
        if (received_at >= deadline_ms || static_cast<size_t>(count) > request)
            return received_at >= deadline_ms ? BodyReadStatus::timeout
                                              : BodyReadStatus::receive_error;
        used += static_cast<size_t>(count);
    }
    return BodyReadStatus::complete;
}
} // namespace monitor
