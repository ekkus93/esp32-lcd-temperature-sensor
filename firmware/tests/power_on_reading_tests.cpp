#include "sensor_registry.hpp"
#include <array>
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
namespace {
uint64_t rom_id() {
    std::array<uint8_t, 7> bytes{0x28, 1, 2, 3, 4, 5, 6};
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes.size(); ++i)
        value |= uint64_t(bytes[i]) << (8 * i);
    return value | (uint64_t(crc8(bytes.data(), bytes.size())) << 56);
}
const Sensor &sensor(const Registry &registry) {
    return registry.sensors().front();
}
} // namespace
int main() {
    const auto id = rom_id();

    // A probe that browned out after Convert T answers exactly 85.0 (its power-on value) with a
    // valid CRC. With no recent reading near 85, that is not believed at once.
    {
        Registry registry;
        registry.sample({{id, 20}}, 0);
        registry.sample({{id, 85}}, 5000);
        CHECK(!sensor(registry).temperature);
        CHECK(sensor(registry).state == State::invalid);
        registry.sample({{id, 20.5}}, 10000);
        CHECK(sensor(registry).temperature && *sensor(registry).temperature == 20.5);
    }

    // Two consecutive readings of exactly 85.0 are accepted: the water really is that hot.
    {
        Registry registry;
        registry.sample({{id, 20}}, 0);
        registry.sample({{id, 85}}, 5000);
        CHECK(!sensor(registry).temperature);
        registry.sample({{id, 85}}, 10000);
        CHECK(sensor(registry).temperature && *sensor(registry).temperature == 85);
        CHECK(sensor(registry).state == State::normal);
        registry.sample({{id, 85}}, 15000);
        CHECK(sensor(registry).temperature && *sensor(registry).temperature == 85);
    }

    // A reading in between, or a missing one, means the second 85 is unconfirmed again.
    {
        Registry registry;
        registry.sample({{id, 85}}, 0);
        registry.sample({{id, 21}}, 5000);
        registry.sample({{id, 85}}, 10000);
        CHECK(!sensor(registry).temperature);
        registry.sample({}, 15000);
        registry.sample({{id, 85}}, 20000);
        CHECK(!sensor(registry).temperature);
    }

    // A probe already near 85 reads it normally.
    {
        Registry registry;
        registry.sample({{id, 84.4}}, 0);
        registry.sample({{id, 85}}, 5000);
        CHECK(sensor(registry).temperature && *sensor(registry).temperature == 85);
        Registry hot;
        hot.sample({{id, 76}}, 0);
        hot.sample({{id, 85}}, 5000);
        CHECK(hot.sensors().front().temperature);
        Registry far;
        far.sample({{id, 70}}, 0);
        far.sample({{id, 85}}, 5000);
        CHECK(!far.sensors().front().temperature); // 15 degrees in one cycle: not believed yet
    }

    // The comparison uses the raw reading, so a calibration offset does not change the rule.
    {
        Registry registry;
        registry.sample({{id, 84.9}}, 0);
        auto settings = sensor(registry).settings;
        settings.calibration = 1.5;
        CHECK(registry.configure(id, settings));
        registry.sample({{id, 84.9}}, 5000);
        registry.sample({{id, 85}}, 10000);
        CHECK(sensor(registry).temperature && *sensor(registry).temperature == 86.5);
    }

    // An unconfirmed 85 must not raise a high alarm; a confirmed one does after its persistence.
    {
        Registry registry;
        registry.sample({{id, 20}}, 0);
        auto settings = sensor(registry).settings;
        settings.high = 40;
        settings.persistence_ms = 0;
        CHECK(registry.configure(id, settings));
        registry.sample({{id, 85}}, 5000);
        CHECK(sensor(registry).state == State::invalid);
        for (const auto &event : registry.events())
            CHECK(event.state != State::high);
        registry.sample({{id, 85}}, 10000);
        CHECK(sensor(registry).state == State::high);
    }

    // An ordinary reading is never affected.
    {
        Registry registry;
        registry.sample({{id, 85.0625}}, 0);
        CHECK(sensor(registry).temperature && *sensor(registry).temperature == 85.0625);
        registry.sample({{id, 84.9375}}, 5000);
        CHECK(sensor(registry).temperature && *sensor(registry).temperature == 84.9375);
    }
    std::cout << "PASS: the DS18B20 power-on value 85.0 is confirmed before it is believed\n";
}
