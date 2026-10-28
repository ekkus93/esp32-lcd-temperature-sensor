#include "wifi_credentials.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <string>

int main() {
    using namespace monitor;
    std::array<uint8_t, kStationRecordV2Size> record{};
    StationCredentials decoded;
    assert(encode_station_credentials("My Network", "test-passphrase", record));
    assert(decode_station_credentials(record.data(), record.size(), decoded));
    assert(decoded.ssid == "My Network" && decoded.password == "test-passphrase");

    // Every single-byte mutation, including the checksum, must be rejected.
    for (size_t i = 0; i < record.size(); ++i) {
        auto corrupted = record;
        corrupted[i] ^= 1;
        StationCredentials untouched{"sentinel", "unchanged"};
        assert(!decode_station_credentials(corrupted.data(), corrupted.size(), untouched));
        assert(untouched.ssid == "sentinel" && untouched.password == "unchanged");
    }
    for (size_t size = 0; size < record.size(); ++size)
        assert(!decode_station_credentials(record.data(), size, decoded));
    assert(!decode_station_credentials(nullptr, record.size(), decoded));

    // Old 99-byte records remain readable without rewriting stored credentials.
    std::array<uint8_t, kStationRecordV1Size> legacy{};
    legacy[0] = 1;
    const std::string old_ssid = "legacy";
    const std::string old_password = "old-password";
    std::copy(old_ssid.begin(), old_ssid.end(), legacy.begin() + 1);
    std::copy(old_password.begin(), old_password.end(), legacy.begin() + 34);
    assert(decode_station_credentials(legacy.data(), legacy.size(), decoded));
    assert(decoded.ssid == old_ssid && decoded.password == old_password);
    legacy[31] = 1; // Noncanonical data after the SSID NUL is rejected.
    assert(!decode_station_credentials(legacy.data(), legacy.size(), decoded));
    legacy[31] = 0;
    legacy[0] = 3;
    assert(!decode_station_credentials(legacy.data(), legacy.size(), decoded));

    assert(encode_station_credentials("open", "", record));
    assert(decode_station_credentials(record.data(), record.size(), decoded));
    assert(decoded.password.empty());
    assert(encode_station_credentials(std::string(32, 's'), std::string(64, 'a'), record));
    assert(decode_station_credentials(record.data(), record.size(), decoded));
    assert(decoded.ssid.size() == 32 && decoded.password.size() == 64);
    assert(!encode_station_credentials("", "12345678", record));
    assert(!encode_station_credentials(std::string(33, 'x'), "12345678", record));
    assert(!encode_station_credentials("wifi", "short", record));
    assert(!encode_station_credentials("wifi", std::string(64, 'z'), record));
    assert(!encode_station_credentials(std::string("wi\0fi", 5), "12345678", record));
    assert(!encode_station_credentials("wifi", std::string("abc\0defgh", 9), record));
    std::cout << "wifi credentials: legacy, v2 CRC, 103 mutations, truncation, bounds PASS\n";
}
