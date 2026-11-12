#pragma once
#include <cstddef>
#include <cstdlib>
#include <new>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace monitor {
// A standard allocator that places a container's storage in PSRAM on the board. Internal RAM is
// scarce and is needed by Wi-Fi, the network stack and task stacks; the firmware's ordinary small
// allocations go there first (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL), so long-lived bulk data such
// as the LCD trend history asks for PSRAM explicitly. Falls back to ordinary memory when PSRAM is
// unavailable, and is plain malloc on the host.
template <class T> struct ExternalAllocator {
    using value_type = T;
    ExternalAllocator() = default;
    template <class U> ExternalAllocator(const ExternalAllocator<U> &) {}
    T *allocate(std::size_t count) {
        const std::size_t bytes = count * sizeof(T);
#ifdef ESP_PLATFORM
        if (void *memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
            return static_cast<T *>(memory);
#endif
        if (void *memory = std::malloc(bytes))
            return static_cast<T *>(memory);
#if defined(__cpp_exceptions)
        throw std::bad_alloc();
#else
        std::abort();
#endif
    }
    void deallocate(T *memory, std::size_t) {
        std::free(memory); // heap_caps_malloc() memory is released with free() as well
    }
    template <class U> bool operator==(const ExternalAllocator<U> &) const {
        return true;
    }
    template <class U> bool operator!=(const ExternalAllocator<U> &) const {
        return false;
    }
};
} // namespace monitor
