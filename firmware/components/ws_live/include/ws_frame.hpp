#pragma once

#include <cstddef>
#include <string_view>
#include <sys/types.h>

namespace monitor::ws_frame {
// A send shim permits deterministic fault-injection tests of the exact firmware
// wire path, without requiring a live ESP32 or TCP window exhaustion.
using Sender = ssize_t (*)(int fd, const void *bytes, size_t length, int flags);

// Send one unmasked RFC 6455 server text frame. Return false on EAGAIN,
// disconnect, or an exhausted bounded-send budget; caller MUST close the
// socket because a partial frame may already be in flight.
bool send_text_nonblocking(int fd, std::string_view payload, Sender send_bytes);
} // namespace monitor::ws_frame
