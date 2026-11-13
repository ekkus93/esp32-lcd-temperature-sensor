#pragma once
// History storage figures as plain values, for the HTTP API ports.
#include <cstddef>
namespace monitor {
struct HistoryStorageInfo {
    size_t total_bytes = 0;
    size_t used_bytes = 0;
    bool available = false;
};
} // namespace monitor
