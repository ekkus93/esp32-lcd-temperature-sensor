#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace monitor {
enum class BodyReadStatus { complete, too_large, eof, receive_error, timeout };
using BodyReceive = int (*)(void *context, char *buffer, size_t size);
using BodyClock = int64_t (*)(void *context);
BodyReadStatus read_exact_body(size_t length, size_t limit, int64_t deadline_ms, void *context,
                               BodyReceive receive, BodyClock clock, std::string &body);
BodyReadStatus discard_exact_body(size_t length, int64_t deadline_ms, void *context,
                                  BodyReceive receive, BodyClock clock);
} // namespace monitor
