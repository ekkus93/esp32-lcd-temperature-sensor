#include "alert_service.hpp"
#include "sensor_registry.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

static uint64_t rom_id(uint8_t serial = 1) {
    std::array<uint8_t, 7> bytes{0x28, serial, 2, 3, 4, 5, 6};
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes.size(); ++i)
        value |= uint64_t(bytes[i]) << (8 * i);
    return value | (uint64_t(crc8(bytes.data(), bytes.size())) << 56);
}

static void sensor_replacement() {
    Registry registry;
    const auto previous = rom_id();
    const auto replacement = rom_id(7);
    CHECK(valid_rom(previous) && valid_rom(replacement));
    registry.sample({{previous, 18}}, 0);
    auto previous_settings = registry.sensors().front().settings;
    previous_settings.name = "Cold room";
    previous_settings.high = 27;
    CHECK(registry.configure(previous, previous_settings));
    registry.sample({{previous, 18}}, sample_ms);
    registry.sample({}, 2 * sample_ms);
    registry.sample({}, 3 * sample_ms);
    registry.sample({}, 4 * sample_ms);
    CHECK(registry.sensors().front().state == State::disconnected);

    registry.sample({{replacement, 22}}, 5 * sample_ms);
    CHECK(registry.sensors().size() == 2);
    CHECK(registry.sensors()[0].rom == previous &&
          registry.sensors()[0].settings.name == "Cold room" &&
          registry.sensors()[0].state == State::disconnected && !registry.sensors()[0].temperature);
    CHECK(registry.sensors()[1].rom == replacement &&
          registry.sensors()[1].settings.name == "Sensor 2" &&
          registry.sensors()[1].state == State::normal && registry.sensors()[1].temperature == 22);
}

static void five_sensor_reconnect() {
    Registry registry;
    std::array<uint64_t, max_sensors> ids{};
    std::vector<Reading> readings;
    for (unsigned i = 0; i < max_sensors; ++i) {
        ids[i] = rom_id(static_cast<uint8_t>(i + 1));
        readings.push_back({ids[i], 18.0 + i});
    }
    registry.sample(readings, 0);
    CHECK(registry.sensors().size() == max_sensors);

    std::map<uint64_t, double> raw_values;
    for (unsigned i = 0; i < max_sensors; ++i) {
        auto settings = registry.sensors()[i].settings;
        settings.name = "Probe " + std::to_string(i + 1);
        settings.high = 60 + i;
        settings.calibration = i * 0.25;
        settings.persistence_ms = 0;
        CHECK(registry.configure(ids[i], settings));
        raw_values[ids[i]] = 18.0 + i;
    }
    registry.sample(readings, sample_ms);

    for (unsigned cycle = 0; cycle < 3; ++cycle)
        registry.sample({}, 2 * sample_ms + cycle * sample_ms);
    for (const auto &sensor : registry.sensors())
        CHECK(sensor.state == State::disconnected && !sensor.temperature);

    std::reverse(readings.begin(), readings.end());
    registry.sample(readings, 5 * sample_ms);
    CHECK(registry.sensors().size() == max_sensors && registry.excess_count() == 0);
    for (const auto &sensor : registry.sensors()) {
        const auto serial = static_cast<unsigned>((sensor.rom >> 8) & 0xff);
        CHECK(serial >= 1 && serial <= max_sensors);
        CHECK(sensor.settings.name == "Probe " + std::to_string(serial));
        CHECK(sensor.settings.high == 59 + serial);
        CHECK(sensor.temperature &&
              *sensor.temperature == raw_values[sensor.rom] + (serial - 1) * 0.25);
        CHECK(sensor.state == State::normal);
    }

    const auto sixth = rom_id(max_sensors + 1);
    readings.push_back({sixth, 24});
    registry.sample(readings, 6 * sample_ms);
    CHECK(registry.sensors().size() == max_sensors && registry.excess_count() == 1);
    CHECK(std::none_of(registry.sensors().begin(), registry.sensors().end(),
                       [&](const Sensor &sensor) { return sensor.rom == sixth; }));
}

int main() {
    sensor_replacement();
    five_sensor_reconnect();
    AlertService alerts;
    Sensor sensor{};
    sensor.rom = 0x28;
    sensor.settings.name = "Freezer";
    sensor.settings.high = 30;
    sensor.settings.low = 10;
    sensor.settings.persistence_ms = 10;
    sensor.settings.hysteresis = 0.5;
    sensor.state = State::normal;

    Reading warm{sensor.rom, 31};
    CHECK(!alerts.update(sensor, &warm, false, 0));
    CHECK(sensor.state == State::normal);
    auto raised = alerts.update(sensor, &warm, false, 10);
    CHECK(raised && raised->previous == State::normal && raised->state == State::high);
    CHECK(sensor.state == State::high);

    Reading hysteresis{sensor.rom, 29.8};
    CHECK(!alerts.update(sensor, &hysteresis, false, 15));
    CHECK(sensor.state == State::high);
    Reading recovered{sensor.rom, 29.4};
    auto cleared = alerts.update(sensor, &recovered, false, 20);
    CHECK(cleared && cleared->previous == State::high && cleared->state == State::normal);

    CHECK(!alerts.update(sensor, nullptr, false, 25));
    CHECK(!alerts.update(sensor, nullptr, false, 30));
    auto disconnected = alerts.update(sensor, nullptr, false, 35);
    CHECK(disconnected && disconnected->state == State::disconnected);
    auto restored = alerts.update(sensor, &recovered, false, 40);
    CHECK(restored && restored->previous == State::disconnected &&
          restored->state == State::normal);

    CHECK(!alerts.update(sensor, nullptr, true, 45));
    CHECK(sensor.state == State::bus_fault && !sensor.temperature);
    auto disabled = alerts.transition(sensor, State::disabled, 50);
    CHECK(!disabled && sensor.state == State::disabled);

    Registry registry;
    const auto id = rom_id();
    CHECK(registry.discover(id));
    auto limits = registry.sensors().front().settings;
    limits.low = 10;
    limits.high = 30;
    limits.persistence_ms = 0;
    CHECK(registry.configure(id, limits));
    for (int i = 0; i < 200; ++i) {
        registry.sample({{id, 35}}, i * 2 * sample_ms);
        registry.sample({{id, 20}}, (i * 2 + 1) * sample_ms);
    }
    const auto &events = registry.events();
    // A flapping sensor (persistence 0) used to write 400 events. Identical transitions are now
    // journaled at most once per 60 s per sensor; the live state still follows every sample.
    // 2,000 s of flapping at 10 s per cycle: at most 34 windows, two transitions each.
    CHECK(events.size() <= 70);
    CHECK(events.size() >= 2);
    CHECK(registry.sensors().front().state == State::normal);
    for (std::size_t i = 1; i < events.size(); ++i)
        CHECK(events[i].sequence == events[i - 1].sequence + 1);
    {
        // Throttling is per sensor and per identical transition: a different transition, or the
        // same one after 60 s, is journaled; the live state is never throttled.
        Registry r;
        const auto a = rom_id();
        CHECK(r.discover(a));
        auto s2 = r.sensors().front().settings;
        s2.low = 10;
        s2.high = 30;
        s2.persistence_ms = 0;
        CHECK(r.configure(a, s2));
        r.sample({{a, 35}}, 0);
        CHECK(r.events().size() == 1);
        r.sample({{a, 20}}, 5000);
        r.sample({{a, 35}}, 10000);
        CHECK(r.events().size() == 3); // invalid->high, high->normal, normal->high
        // The same two transitions again inside 60 s are not journaled; the state is current.
        r.sample({{a, 20}}, 15000);
        CHECK(r.sensors().front().state == State::normal);
        r.sample({{a, 35}}, 20000);
        CHECK(r.sensors().front().state == State::high);
        CHECK(r.events().size() == 3);
        // After the window both are journaled again.
        r.sample({{a, 20}}, 70000);
        r.sample({{a, 35}}, 75000);
        CHECK(r.events().size() == 5);
    }
    {
        // No registered sensor is ever State::unassigned; that value is a display-slot value.
        Registry r;
        const auto a = rom_id();
        CHECK(r.discover(a));
        CHECK(r.sensors().front().state != State::unassigned);
        r.sample({}, 5000);
        r.expire(100000);
        for (const auto &sensor : r.sensors())
            CHECK(sensor.state != State::unassigned);
    }
    std::cout << "PASS: alert hysteresis, recovery, bounded event burst and bus/disable states\n";
}
