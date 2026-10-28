#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace monitor {
constexpr size_t kStationRecordV1Size = 99;
constexpr size_t kStationRecordV2Size = 103;

struct StationCredentials {
    std::string ssid;
    std::string password;
};

inline bool valid_station_credentials(std::string_view ssid, std::string_view password) {
    if (ssid.empty() || ssid.size() > 32 || password.size() > 64 ||
        (!password.empty() && password.size() < 8) ||
        ssid.find('\0') != std::string_view::npos ||
        password.find('\0') != std::string_view::npos)
        return false;
    if (password.size() == 64)
        return std::all_of(password.begin(), password.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                   (c >= 'A' && c <= 'F');
        });
    return true;
}

// CRC32 detects accidental NVS corruption, not malicious tampering or disclosure.
inline uint32_t station_record_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}

inline bool encode_station_credentials(std::string_view ssid, std::string_view password,
                                       std::array<uint8_t, kStationRecordV2Size> &record) {
    if (!valid_station_credentials(ssid, password))
        return false;
    record.fill(0);
    record[0] = 2;
    std::memcpy(record.data() + 1, ssid.data(), ssid.size());
    if (!password.empty())
        std::memcpy(record.data() + 34, password.data(), password.size());
    const uint32_t crc = station_record_crc32(record.data(), kStationRecordV1Size);
    for (unsigned i = 0; i < 4; ++i)
        record[kStationRecordV1Size + i] = static_cast<uint8_t>(crc >> (8 * i));
    return true;
}

inline bool decode_station_credentials(const uint8_t *record, size_t size,
                                       StationCredentials &credentials) {
    if (!record || !((size == kStationRecordV1Size && record[0] == 1) ||
                     (size == kStationRecordV2Size && record[0] == 2)))
        return false;
    if (size == kStationRecordV2Size) {
        const uint32_t expected = station_record_crc32(record, kStationRecordV1Size);
        for (unsigned i = 0; i < 4; ++i)
            if (record[kStationRecordV1Size + i] != static_cast<uint8_t>(expected >> (8 * i)))
                return false;
    }
    const uint8_t *ssid_end = std::find(record + 1, record + 34, uint8_t{0});
    const uint8_t *password_end = std::find(record + 34, record + 99, uint8_t{0});
    if (ssid_end == record + 34 || password_end == record + 99 ||
        !std::all_of(ssid_end, record + 34, [](uint8_t b) { return b == 0; }) ||
        !std::all_of(password_end, record + 99, [](uint8_t b) { return b == 0; }))
        return false;
    const std::string_view ssid(reinterpret_cast<const char *>(record + 1),
                                static_cast<size_t>(ssid_end - (record + 1)));
    const std::string_view password(reinterpret_cast<const char *>(record + 34),
                                    static_cast<size_t>(password_end - (record + 34)));
    if (!valid_station_credentials(ssid, password))
        return false;
    credentials = {std::string(ssid), std::string(password)};
    return true;
}
} // namespace monitor
