#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace monitor {
inline bool history_partition_erased(const uint8_t *bytes, size_t size) {
    return bytes && size > 0 &&
           std::all_of(bytes, bytes + size, [](uint8_t byte) { return byte == 0xff; });
}

inline bool may_format_history_partition(bool mount_failed, bool partition_erased) {
    return mount_failed && partition_erased;
}
} // namespace monitor
