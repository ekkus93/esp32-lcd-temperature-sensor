#include "fake_bus.hpp"
namespace monitor {
std::vector<Reading> fake_readings(unsigned count) {
    std::vector<Reading> values;
    for (unsigned i = 0; i < count && i < max_sensors; ++i) {
        uint8_t bytes[7] = {0x28, static_cast<uint8_t>(i + 1)};
        uint64_t id = 0x28 | (uint64_t(i + 1) << 8) | (uint64_t(crc8(bytes, 7)) << 56);
        values.push_back({id, 20.0 + i, true});
    }
    return values;
}
} // namespace monitor
