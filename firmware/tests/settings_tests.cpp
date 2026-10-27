#include "settings_codec.hpp"
#include <cstdlib>
#include <iostream>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
uint64_t rom(unsigned serial) {
    uint64_t value = 0x28 | (uint64_t(serial) << 8);
    uint8_t b[7] = {0x28, uint8_t(serial)};
    return value | (uint64_t(crc8(b, 7)) << 56);
}
int main() {
    Registry registry;
    registry.discover(rom(1));
    registry.discover(rom(2));
    auto s = registry.sensors()[0].settings;
    s.name = "Freezer \"A\"";
    s.short_name = "Cold A";
    s.enabled = false;
    s.low = -25;
    s.high = -10;
    s.calibration = -.125;
    s.order = 4;
    CHECK(registry.configure(rom(1), s));
    auto other = registry.sensors()[1].settings;
    other.name = "Basement";
    other.short_name = "Base";
    other.low = 8;
    other.high = 25;
    other.calibration = 2.5;
    other.order = 1;
    CHECK(registry.configure(rom(2), other));
    auto bytes = encode_settings(registry.sensors());
    CHECK(!bytes.empty());
    auto restored = decode_settings(bytes);
    CHECK(restored && restored->size() == 2);
    CHECK((*restored)[0].settings.name == s.name && !(*restored)[0].settings.enabled);
    CHECK((*restored)[0].settings.calibration == -.125 && (*restored)[0].settings.order == 4);
    CHECK((*restored)[1].rom == rom(2) && (*restored)[1].settings.name == "Basement");
    CHECK((*restored)[1].settings.low == 8 && (*restored)[1].settings.high == 25 &&
          (*restored)[1].settings.calibration == 2.5);
    Registry reboot;
    for (auto saved = restored->rbegin(); saved != restored->rend(); ++saved) {
        CHECK(reboot.discover(saved->rom));
        CHECK(reboot.configure(saved->rom, saved->settings));
    }
    reboot.sample({{rom(2), 22}, {rom(1), -20}}, 5000);
    CHECK(reboot.sensors()[0].rom == rom(2) && reboot.sensors()[0].settings.name == "Basement");
    CHECK(reboot.sensors()[0].temperature == 24.5 && reboot.sensors()[0].settings.low == 8 &&
          reboot.sensors()[0].settings.high == 25);
    CHECK(reboot.sensors()[1].rom == rom(1) && reboot.sensors()[1].settings.name == s.name &&
          reboot.sensors()[1].settings.calibration == -.125 && !reboot.sensors()[1].temperature);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto bad = bytes;
        bad[i] ^= 1;
        CHECK(!decode_settings(bad));
    }
    for (std::size_t i = 0; i < bytes.size(); ++i)
        CHECK(!decode_settings(bytes.substr(0, i)));
    auto newer = bytes;
    newer[3] = '2';
    CHECK(!decode_settings(newer));
    auto duplicate = registry.sensors();
    duplicate.push_back(duplicate[0]);
    CHECK(encode_settings(duplicate).empty());
    CHECK(!decode_settings(std::string(5000, 'x')));
    auto invalid = s;
    invalid.name = "line\nbreak";
    CHECK(!valid_settings(invalid));
    invalid.name = std::string("bad\0name", 8);
    CHECK(!valid_settings(invalid));
    invalid.name = "\xc0\xaf";
    CHECK(!valid_settings(invalid));
    invalid.name = "Café";
    CHECK(valid_settings(invalid));
    CHECK(decode_settings(encode_settings({}))->empty());
    std::cout << "PASS: configuration roundtrip, reboot identity, corruption, truncation, schema "
                 "and size limits\n";
}
