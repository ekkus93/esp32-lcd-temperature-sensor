#include "ws_frame.hpp"

#include <cerrno>
#include <cstdint>
#include <limits>
#include <sys/socket.h>

namespace monitor::ws_frame {
namespace {
constexpr unsigned kMaxSendCallsPerFrame = 16;

bool send_bytes_nonblocking(int fd, const uint8_t *data, size_t length, Sender sender,
                            unsigned &calls_remaining) {
    size_t sent = 0;
    while (sent < length && calls_remaining) {
        --calls_remaining;
        const ssize_t result = sender(fd, data + sent, length - sent, MSG_DONTWAIT);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0 || size_t(result) > length - sent)
            return false;
        sent += size_t(result);
    }
    return sent == length;
}
} // namespace

bool send_text_nonblocking(int fd, std::string_view payload, Sender sender) {
    if (!sender || payload.size() > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return false;
    uint8_t header[10]{};
    size_t header_length = 2;
    header[0] = 0x81; // FIN + server-originated text; payload is never masked.
    if (payload.size() <= 125) {
        header[1] = uint8_t(payload.size());
    } else if (payload.size() <= UINT16_MAX) {
        header[1] = 126;
        header[2] = uint8_t(payload.size() >> 8);
        header[3] = uint8_t(payload.size());
        header_length = 4;
    } else {
        header[1] = 127;
        const auto length = uint64_t(payload.size());
        for (unsigned index = 0; index < 8; ++index)
            header[2 + index] = uint8_t(length >> ((7 - index) * 8));
        header_length = 10;
    }
    unsigned calls_remaining = kMaxSendCallsPerFrame;
    return send_bytes_nonblocking(fd, header, header_length, sender, calls_remaining) &&
           send_bytes_nonblocking(fd, reinterpret_cast<const uint8_t *>(payload.data()),
                                  payload.size(), sender, calls_remaining);
}
} // namespace monitor::ws_frame
