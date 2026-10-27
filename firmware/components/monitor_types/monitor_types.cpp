#include "monitor_types.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace monitor {
const char *state_name(State state) {
    static constexpr const char *names[] = {"normal",     "high",     "low",
                                            "unassigned", "disabled", "disconnected",
                                            "stale",      "invalid",  "bus_fault"};
    return names[static_cast<unsigned>(state)];
}
uint8_t crc8(const uint8_t *data, std::size_t length) {
    uint8_t crc = 0;
    for (std::size_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            bool mix = (crc ^ byte) & 1;
            crc >>= 1;
            if (mix)
                crc ^= 0x8c;
            byte >>= 1;
        }
    }
    return crc;
}
bool valid_rom(uint64_t rom) {
    std::array<uint8_t, 8> bytes{};
    for (unsigned i = 0; i < 8; ++i)
        bytes[i] = (rom >> (i * 8)) & 0xff;
    return bytes[0] == 0x28 && crc8(bytes.data(), 7) == bytes[7];
}
bool bus_fault_detected(bool line_low, bool operation_failed, std::size_t expected_devices,
                        std::size_t completed_transactions) {
    return line_low || operation_failed || (expected_devices > 0 && completed_transactions == 0);
}
std::string rom_hex(uint64_t rom) {
    char out[17]{};
    for (unsigned i = 0; i < 8; ++i)
        std::snprintf(out + i * 2, 3, "%02x", unsigned((rom >> (i * 8)) & 0xff));
    return out;
}
std::optional<double> decode_temperature(const std::array<uint8_t, 9> &data, bool complete) {
    if (!complete || crc8(data.data(), 8) != data[8])
        return std::nullopt;
    int16_t raw = static_cast<int16_t>((uint16_t(data[1]) << 8) | data[0]);
    double value = raw / 16.0;
    if (value < -55 || value > 125)
        return std::nullopt;
    return value;
}
namespace {
bool label_text(const std::string &text) {
    for (std::size_t i = 0; i < text.size();) {
        unsigned char lead = text[i++];
        if (lead < 0x20 || lead == 0x7f)
            return false;
        if (lead < 0x80)
            continue;
        unsigned remaining = 0;
        uint32_t code = 0;
        uint32_t minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) {
            remaining = 1;
            code = lead & 0x1f;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            remaining = 2;
            code = lead & 0x0f;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            remaining = 3;
            code = lead & 7;
            minimum = 0x10000;
        } else
            return false;
        if (i + remaining > text.size())
            return false;
        for (unsigned j = 0; j < remaining; ++j) {
            unsigned char next = text[i++];
            if ((next & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return false;
    }
    return true;
}
} // namespace
bool valid_settings(const Settings &s) {
    return !s.name.empty() && s.name.size() <= 32 && s.short_name.size() <= 12 &&
           label_text(s.name) && label_text(s.short_name) && s.order < max_sensors &&
           std::isfinite(s.calibration) && std::abs(s.calibration) <= 20 &&
           std::isfinite(s.hysteresis) && s.hysteresis >= 0 && s.hysteresis <= 20 &&
           s.persistence_ms >= 0 && s.persistence_ms <= 3600000 &&
           (!s.low || (std::isfinite(*s.low) && *s.low >= -55 && *s.low <= 125)) &&
           (!s.high || (std::isfinite(*s.high) && *s.high >= -55 && *s.high <= 125)) &&
           (!s.low || !s.high || *s.low < *s.high);
}
} // namespace monitor
