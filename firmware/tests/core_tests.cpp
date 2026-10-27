#include "fake_bus.hpp"
#include "monitor.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __FILE__ << ':' << __LINE__ << " check failed: " #expr << '\n';           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
uint64_t rom(unsigned serial) {
    uint64_t value = 0x28 | (uint64_t(serial) << 8);
    std::array<uint8_t, 8> bytes{};
    for (unsigned i = 0; i < 7; ++i)
        bytes[i] = (value >> (8 * i)) & 0xff;
    return value | (uint64_t(crc8(bytes.data(), 7)) << 56);
}
void identity() {
    Registry registry;
    CHECK(registry.sensors().empty());
    auto a = rom(1), b = rom(2);
    CHECK(valid_rom(a));
    CHECK(!valid_rom(a ^ 0x100));
    registry.sample({{b, 22}, {a, 20}, {a, 20}}, 0);
    CHECK(registry.sensors().size() == 2);
    Settings settings = registry.sensors()[0].settings;
    auto original = registry.sensors()[0].rom;
    settings.name = "Freezer";
    CHECK(registry.configure(original, settings));
    registry.sample({{a, 19}, {b, 21}}, 5000);
    CHECK(registry.sensors()[0].rom == original);
    CHECK(registry.sensors()[0].settings.name == "Freezer");
    for (unsigned i = 3; i <= 6; ++i)
        registry.discover(rom(i));
    CHECK(registry.sensors().size() == 5);
    CHECK(registry.excess_count() == 1);
    settings.low = 30;
    settings.high = 20;
    CHECK(!registry.configure(original, settings));
}
void invalid_and_faults() {
    CHECK(bus_fault_detected(true, false, 0, 0));
    CHECK(bus_fault_detected(false, true, 0, 0));
    CHECK(bus_fault_detected(false, false, 5, 0));
    CHECK(!bus_fault_detected(false, false, 5, 1));
    CHECK(!bus_fault_detected(false, false, 0, 0));
    Registry registry;
    auto id = rom(1);
    registry.sample({{id, 85, false}}, 0);
    CHECK(!registry.sensors()[0].temperature);
    registry.sample({{id, 85, true}}, 5000);
    CHECK(registry.sensors()[0].temperature == 85);
    registry.sample({{id, -127}}, 10000);
    CHECK(!registry.sensors()[0].temperature);
    registry.sample({}, 15000);
    registry.sample({}, 20000);
    registry.sample({}, 21000);
    CHECK(registry.sensors()[0].state == State::disconnected);
    registry.sample({{id, 25}}, 25000);
    CHECK(registry.sensors()[0].state == State::normal);
    registry.expire(45001);
    CHECK(registry.sensors()[0].state == State::stale);
    registry.sample({}, 50000, true);
    auto count = registry.events().size();
    CHECK(registry.sensors()[0].state == State::bus_fault);
    registry.sample({}, 55000, true);
    CHECK(registry.events().size() == count);
    auto settings = registry.sensors()[0].settings;
    settings.enabled = false;
    CHECK(registry.configure(id, settings));
    registry.sample({}, 60000, true);
    CHECK(registry.sensors()[0].state == State::disabled);
    CHECK(!registry.sensors()[0].temperature);
    std::array<uint8_t, 9> data{0x90, 0x01, 0x4b, 0x46, 0x7f, 0xff, 0x0c, 0x10, 0};
    data[8] = crc8(data.data(), 8);
    CHECK(decode_temperature(data, true) == 25);
    CHECK(!decode_temperature(data, false));
    data[1] ^= 1;
    CHECK(!decode_temperature(data, true));
}
void alarms() {
    Registry registry;
    auto id = rom(7);
    registry.discover(id);
    auto settings = registry.sensors()[0].settings;
    settings.high = 30;
    settings.low = 10;
    settings.calibration = 1;
    CHECK(registry.configure(id, settings));
    registry.sample({{id, 30}}, 0);
    registry.sample({{id, 30}}, 10000);
    CHECK(registry.sensors()[0].state == State::normal);
    registry.sample({{id, 30}}, 15000);
    CHECK(registry.sensors()[0].state == State::high);
    registry.sample({{id, 28.8}}, 20000);
    CHECK(registry.sensors()[0].state == State::high);
    registry.sample({{id, 28.4}}, 25000);
    CHECK(registry.sensors()[0].state == State::normal);
    CHECK(registry.events().size() == 2);
    registry.sample({{id, 8}}, 30000);
    registry.sample({{id, 8}}, 45000);
    CHECK(registry.sensors()[0].state == State::low);
}
void history_and_clock() {
    Registry registry;
    Aggregator history(123);
    auto id = rom(9);
    for (int64_t now = 0; now < bucket_ms; now += sample_ms) {
        registry.sample({{id, double(now / sample_ms)}}, now);
        CHECK(!history.add(registry, now));
    }
    registry.sample({{id, 60}}, bucket_ms);
    auto first = history.add(registry, bucket_ms);
    CHECK(first && first->sensors.size() == 1);
    CHECK(first->sensors[0].count == 60);
    CHECK(first->sensors[0].average == 29.5);
    CHECK(first->sensors[0].minimum == 0 && first->sensors[0].maximum == 59);
    CHECK(!first->utc_seconds && first->boot_id == 123 && !first->sensors[0].partial);
    registry.sample({}, bucket_ms + sample_ms);
    CHECK(!history.add(registry, bucket_ms + sample_ms));
    registry.sample({{id, 10}}, bucket_ms * 2);
    auto partial = history.add(registry, bucket_ms * 2);
    CHECK(partial && partial->sensors[0].count == 1 && partial->sensors[0].partial);
    Clock clock;
    CHECK(!clock.utc(100000));
    clock.synchronize(100000, 1800000000);
    CHECK(clock.utc(105000) == 1800000005);
    // The monotonic wall-clock estimate continues while Wi-Fi is unavailable.
    CHECK(clock.utc(705000) == 1800000605);
    auto split = history.add(registry, bucket_ms * 2 + sample_ms, 1800000000);
    CHECK(split && !split->utc_seconds && split->sensors[0].partial);

    Aggregator sync_recovery(77);
    registry.sample({{id, 18}}, 0);
    CHECK(!sync_recovery.add(registry, 0));
    registry.sample({{id, 19}}, 295000);
    auto unknown_window = sync_recovery.add(registry, 295000, 1800000120);
    CHECK(unknown_window && unknown_window->boot_id == 77 && !unknown_window->utc_seconds &&
          unknown_window->sensors[0].partial);
    registry.sample({{id, 20}}, 595000);
    auto trusted_window = sync_recovery.add(registry, 595000, 1800000420);
    CHECK(trusted_window && trusted_window->boot_id == 77 &&
          trusted_window->utc_seconds == 1800000000 && trusted_window->sensors[0].partial);
}
void configuration_transitions() {
    Registry registry;
    auto id = rom(23);
    registry.discover(id);
    auto settings = registry.sensors()[0].settings;
    settings.high = 10;
    settings.persistence_ms = 0;
    CHECK(registry.configure(id, settings, 0));
    registry.sample({{id, 20}}, 0);
    CHECK(registry.sensors()[0].state == State::high);
    auto count = registry.events().size();
    settings.name = "Renamed";
    CHECK(registry.configure(id, settings, 1000));
    CHECK(registry.sensors()[0].state == State::high && registry.sensors()[0].temperature == 20);
    CHECK(registry.events().size() == count);
    settings.enabled = false;
    CHECK(registry.configure(id, settings, 2000));
    CHECK(registry.sensors()[0].state == State::disabled && !registry.sensors()[0].temperature);
    CHECK(registry.events().size() == count + 1 && registry.events().back().uptime_ms == 2000);
    settings.enabled = true;
    settings.high.reset();
    settings.low = 10;
    settings.hysteresis = 1;
    CHECK(registry.configure(id, settings, 3000));
    registry.sample({{id, 9}}, 3000);
    CHECK(registry.sensors()[0].state == State::low);
    registry.sample({{id, 10.5}}, 8000);
    CHECK(registry.sensors()[0].state == State::low);
    registry.sample({{id, 11.1}}, 13000);
    CHECK(registry.sensors()[0].state == State::normal);
}
void bus_permutations() {
    for (unsigned count = 0; count <= max_sensors; ++count) {
        Registry registry;
        auto readings = fake_readings(count);
        registry.sample(readings, 0);
        CHECK(registry.sensors().size() == count);
        auto original = registry.sensors();
        std::reverse(readings.begin(), readings.end());
        if (!readings.empty())
            readings.push_back(readings.front());
        registry.sample(readings, 5000);
        CHECK(registry.sensors().size() == count);
        for (unsigned i = 0; i < count; ++i)
            CHECK(registry.sensors()[i].rom == original[i].rom);
        registry.sample({}, 10000, true);
        auto events = registry.events().size();
        registry.sample({}, 15000, true);
        CHECK(registry.events().size() == events);
        registry.sample(fake_readings(count), 20000);
        CHECK(!registry.bus_fault());
        for (const auto &sensor : registry.sensors())
            CHECK(sensor.state == State::normal && sensor.temperature);
    }
}
void bucket_alignment() {
    Registry registry;
    auto id = rom(19);
    Aggregator history(71);
    for (int64_t now = 15; now < bucket_ms; now += sample_ms) {
        registry.sample({{id, 20}}, now);
        CHECK(!history.add(registry, now));
    }
    registry.sample({{id, 21}}, 300009);
    auto first = history.add(registry, 300009);
    CHECK(first && first->start_uptime_ms == 0 && first->sensors[0].count == 60 &&
          !first->sensors[0].partial);
    Aggregator trusted(72);
    for (int64_t now = 0; now < 295000; now += sample_ms) {
        registry.sample({{id, 20}}, now);
        CHECK(!trusted.add(registry, now, 1800000005 + now / 1000));
    }
    registry.sample({{id, 21}}, 295000);
    auto aligned = trusted.add(registry, 295000, 1800000300);
    CHECK(aligned && aligned->utc_seconds == 1800000000 && aligned->sensors[0].count == 59 &&
          aligned->sensors[0].partial);

    Aggregator before_restart(73);
    for (int64_t now = 0; now < 10000; now += sample_ms) {
        registry.sample({{id, 30}}, now);
        CHECK(!before_restart.add(registry, now));
    }
    Aggregator after_restart(74);
    for (int64_t now = 0; now <= bucket_ms; now += sample_ms) {
        registry.sample({{id, 10}}, now);
        auto bucket = after_restart.add(registry, now);
        if (now == bucket_ms)
            CHECK(bucket && bucket->boot_id == 74 && bucket->sensors[0].count == 60 &&
                  bucket->sensors[0].average == 10);
        else
            CHECK(!bucket);
    }

    Aggregator jumped(75);
    registry.sample({{id, 20}}, 0);
    CHECK(!jumped.add(registry, 0, 1800000000));
    registry.sample({{id, 20}}, sample_ms);
    auto split = jumped.add(registry, sample_ms, 1800000105);
    CHECK(split && split->boot_id == 75 && split->utc_seconds == 1800000000 &&
          split->sensors[0].count == 1 && split->sensors[0].partial);

    Aggregator stepped(76);
    registry.sample({{id, 20}}, 0);
    CHECK(!stepped.add(registry, 0, 1800000000));
    registry.sample({{id, 21}}, sample_ms);
    auto before_step = stepped.add(registry, sample_ms, 1800000105);
    CHECK(before_step && before_step->utc_seconds == 1800000000 && before_step->sensors[0].partial);
    registry.sample({{id, 22}}, 305000);
    auto unsynced = stepped.add(registry, 305000, 1800000405);
    CHECK(unsynced && !unsynced->utc_seconds && unsynced->start_uptime_ms == sample_ms &&
          unsynced->sensors[0].partial);
    registry.sample({{id, 23}}, 605000);
    auto recovered = stepped.add(registry, 605000, 1800000705);
    CHECK(recovered && recovered->utc_seconds == 1800000300 && recovered->sensors[0].partial);
}
void aggregate_metadata() {
    Registry registry;
    const auto id = rom(31);
    registry.sample({{id, 10}}, 0);
    Aggregator history(90);
    for (int64_t now = 0; now <= 600000; now += sample_ms) {
        if (now == 150000) {
            auto settings = registry.sensors()[0].settings;
            settings.enabled = false;
            settings.name = "Renamed freezer";
            CHECK(registry.configure(id, settings, now));
        }
        registry.sample({{id, 10}}, now);
        auto bucket = history.add(registry, now);
        if (now == 300000) {
            CHECK(bucket && bucket->sensors[0].name == "Renamed freezer");
            CHECK(bucket->sensors[0].count == 30 && bucket->sensors[0].partial);
            CHECK(bucket->sensors[0].state_flags ==
                  ((1u << unsigned(State::normal)) | (1u << unsigned(State::disabled))));
        }
        if (now == 600000) {
            CHECK(bucket && bucket->sensors[0].count == 0 && bucket->sensors[0].partial);
            CHECK(bucket->sensors[0].average == 0 &&
                  bucket->sensors[0].state_flags == (1u << unsigned(State::disabled)));
        }
    }
}
void controls() {
    DisplayCycle display;
    CHECK(!display.trends(100000));
    CHECK(display.configure(true, 20, 10, 1000));
    CHECK(!display.trends(20999));
    CHECK(display.trends(21000));
    CHECK(!display.trends(31000));
    CHECK(!display.configure(true, 0, 10, 1000));
    BootButton button;
    CHECK(!button.update(true, 0));
    CHECK(!button.update(true, 30));
    CHECK(!button.update(true, 10000)); // Held during startup never triggers.
    CHECK(!button.update(false, 10001));
    CHECK(!button.update(false, 10031));
    CHECK(!button.update(true, 11000));
    CHECK(!button.update(false, 11010)); // Bounce.
    CHECK(!button.update(true, 11020));
    CHECK(!button.update(true, 11050));
    CHECK(!button.update(true, 16049));
    CHECK(button.update(true, 16050));
    CHECK(!button.update(true, 25000));
    CHECK(button.progress(25000) == 100);
}
int run_core_tests() {
    identity();
    invalid_and_faults();
    alarms();
    history_and_clock();
    configuration_transitions();
    bus_permutations();
    bucket_alignment();
    aggregate_metadata();
    controls();
    std::cout << "PASS: identity, CRC, invalid data, alerts, aggregates, clock, display and BOOT\n";
    return 0;
}

#ifndef ESP_PLATFORM
int main() {
    return run_core_tests();
}
#endif
