#include "alert_service.hpp"
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

int main() {
    sensor_replacement();
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
    CHECK(events.size() == 128);
    CHECK(events.front().sequence == 273 && events.back().sequence == 400);
    for (std::size_t i = 1; i < events.size(); ++i)
        CHECK(events[i].sequence == events[i - 1].sequence + 1);
    std::cout << "PASS: alert hysteresis, recovery, bounded event burst and bus/disable states\n";
}
