// The ice-bath calibration hold (spec 17.3): while a probe is held it raises no high or low alarm
// and its readings are not recorded; disconnects still alarm; the hold ends on its own, early, or
// with the probe.
#include "display_model.hpp"
#include "monitor.hpp"
#include "sensor_registry.hpp"
#include <algorithm>
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
uint64_t rom_id(unsigned serial) {
    std::array<uint8_t, 7> bytes{0x28, uint8_t(serial), 2, 3, 4, 5, 6};
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes.size(); ++i)
        value |= uint64_t(bytes[i]) << (8 * i);
    return value | (uint64_t(crc8(bytes.data(), bytes.size())) << 56);
}
const Sensor &sensor(const Registry &registry, uint64_t id) {
    for (const auto &item : registry.sensors())
        if (item.rom == id)
            return item;
    std::cerr << "sensor missing\n";
    std::exit(1);
}
size_t events_for(const Registry &registry, uint64_t id) {
    const auto &events = registry.events();
    return std::count_if(events.begin(), events.end(),
                         [id](const Event &event) { return event.rom == id; });
}
// A probe with a 30 degree high limit, 0.5 degree hysteresis and 15 s persistence.
Registry limited_registry(uint64_t id) {
    Registry registry;
    CHECK(registry.discover(id));
    auto settings = sensor(registry, id).settings;
    settings.high = 30;
    settings.hysteresis = 0.5;
    settings.persistence_ms = 15000;
    CHECK(registry.configure(id, settings));
    return registry;
}
// Samples a steady reading every 5 s from `from` for `count` samples; returns the next time.
int64_t sample_for(Registry &registry, uint64_t id, double celsius, int64_t from, int count) {
    int64_t now = from;
    for (int i = 0; i < count; ++i, now += sample_ms)
        registry.sample({{id, celsius}}, now);
    return now;
}

void hold_needs_a_connected_probe() {
    Registry registry;
    CHECK(!registry.hold_calibration(rom_id(1), 0, 60000)); // unknown
    const auto id = rom_id(1);
    CHECK(registry.discover(id));
    CHECK(!registry.hold_calibration(id, 0, 60000)); // never read
    registry.sample({{id, std::nullopt}}, 1000);     // a failed read: invalid, not connected
    CHECK(!registry.hold_calibration(id, 6000, 60000));
    registry.sample({{id, 20.0}}, 11000);
    CHECK(registry.hold_calibration(id, 11000, 60000));
    CHECK(sensor(registry, id).calibration_hold_until_ms == 71000);
}

void held_probe_raises_no_high_alarm() {
    const auto id = rom_id(2);
    auto registry = limited_registry(id);
    registry.sample({{id, 20.0}}, 0);
    CHECK(registry.hold_calibration(id, 0, 15 * 60000));
    // Well above the limit for much longer than the persistence time, as a bath would not be.
    sample_for(registry, id, 35.0, 0, 60);
    CHECK(sensor(registry, id).state == State::normal);
    CHECK(events_for(registry, id) == 0);
}

void starting_a_hold_clears_an_active_high_alarm() {
    const auto id = rom_id(3);
    auto registry = limited_registry(id);
    int64_t now = sample_for(registry, id, 35.0, 0, 6); // 30 s: past persistence, high
    CHECK(sensor(registry, id).state == State::high);
    const size_t before = events_for(registry, id);
    CHECK(registry.hold_calibration(id, now, 15 * 60000));
    registry.sample({{id, 35.0}}, now);
    CHECK(sensor(registry, id).state == State::normal);
    CHECK(events_for(registry, id) == before + 1);
    const Event &last = registry.events().back();
    CHECK(last.rom == id && last.previous == State::high && last.state == State::normal);
}

void high_alarm_returns_after_the_hold_with_its_persistence_time() {
    const auto id = rom_id(4);
    auto registry = limited_registry(id);
    registry.sample({{id, 20.0}}, 0);
    CHECK(registry.hold_calibration(id, 0, 60000));
    // The bath ends at 60 s. The probe is still above the limit (warm after leaving the ice).
    int64_t now = sample_for(registry, id, 35.0, 0, 12); // 0 .. 55 s, held
    CHECK(sensor(registry, id).state == State::normal);
    now = sample_for(registry, id, 35.0, now, 1);       // 60 s: the hold has ended
    CHECK(sensor(registry, id).state == State::normal); // persistence starts now
    CHECK(!sensor(registry, id).calibration_hold_until_ms);
    // Persistence is 15 s from the first sample after the hold.
    now = sample_for(registry, id, 35.0, now, 2); // 65 s, 70 s
    CHECK(sensor(registry, id).state == State::normal);
    sample_for(registry, id, 35.0, now, 1); // 75 s: persisted
    CHECK(sensor(registry, id).state == State::high);
}

void disconnect_still_alarms_during_a_hold() {
    const auto id = rom_id(5);
    auto registry = limited_registry(id);
    registry.sample({{id, 20.0}}, 0);
    CHECK(registry.hold_calibration(id, 0, 15 * 60000));
    // Three cycles with no reading: the probe is gone, and that alarm is not held back.
    for (int64_t t = 5000; t <= 15000; t += sample_ms)
        registry.sample({}, t);
    CHECK(sensor(registry, id).state == State::disconnected);
    CHECK(events_for(registry, id) == 1);
}

void held_readings_are_not_recorded() {
    const auto id = rom_id(6);
    Registry registry;
    CHECK(registry.discover(id));
    registry.sample({{id, 20.0}}, 0);
    CHECK(registry.hold_calibration(id, 0, 15 * 60000));
    Aggregator aggregator(1);
    int64_t now = 0;
    for (size_t i = 0; i < 60; ++i, now += sample_ms) {
        registry.sample({{id, 20.0}}, now);
        CHECK(!aggregator.add(registry, now));
    }
    registry.sample({{id, 20.0}}, now);
    const auto bucket = aggregator.add(registry, now);
    CHECK(bucket && bucket->sensors.size() == 1);
    // The bucket exists, but holds no readings for the bath: the history shows a gap.
    CHECK(bucket->sensors[0].count == 0);
}

void hold_ends_on_its_own_and_renewal_replaces_it() {
    const auto id = rom_id(7);
    auto registry = limited_registry(id);
    registry.sample({{id, 20.0}}, 0);
    CHECK(registry.hold_calibration(id, 0, 60000));
    CHECK(registry.hold_calibration(id, 30000, 60000)); // renewed: now until 90 s
    CHECK(sensor(registry, id).calibration_hold_until_ms == 90000);
    registry.sample({{id, 20.0}}, 89000);
    CHECK(sensor(registry, id).calibration_hold_until_ms == 90000);
    registry.sample({{id, 20.0}}, 90000);
    CHECK(!sensor(registry, id).calibration_hold_until_ms);
}

void a_hold_is_capped_at_twenty_minutes() {
    const auto id = rom_id(8);
    Registry registry;
    CHECK(registry.discover(id));
    registry.sample({{id, 20.0}}, 0);
    CHECK(registry.hold_calibration(id, 0, 60 * 60000));
    CHECK(sensor(registry, id).calibration_hold_until_ms == max_calibration_hold_ms);
    CHECK(!registry.hold_calibration(id, 0, 0)); // a zero or negative duration is refused
}

void ending_a_hold_early_clears_it() {
    const auto id = rom_id(9);
    Registry registry;
    CHECK(registry.discover(id));
    registry.sample({{id, 20.0}}, 0);
    CHECK(registry.hold_calibration(id, 0, 15 * 60000));
    CHECK(registry.end_calibration_hold(id));
    CHECK(!sensor(registry, id).calibration_hold_until_ms);
    CHECK(!registry.end_calibration_hold(rom_id(10))); // unknown probe
}

void retiring_a_probe_ends_its_hold() {
    const auto id = rom_id(11);
    Registry registry;
    CHECK(registry.discover(id));
    registry.sample({{id, 20.0}}, 0);
    CHECK(registry.hold_calibration(id, 0, 15 * 60000));
    for (int64_t t = 5000; t <= 15000; t += sample_ms)
        registry.sample({}, t);
    CHECK(registry.retire(id));
    CHECK(registry.sensors().empty());
    // Discovered again later, it starts with no hold.
    CHECK(registry.discover(id));
    CHECK(!sensor(registry, id).calibration_hold_until_ms);
}
} // namespace

// The LCD shows CAL in place of a probe's status while its hold is in force (spec 17.3).
void lcd_shows_cal_while_held() {
    const auto id = rom_id(12);
    Registry registry;
    CHECK(registry.discover(id));
    registry.sample({{id, 20.0}}, 0);
    auto rows = display_rows(registry, false);
    CHECK(rows[0].status.empty());
    CHECK(registry.hold_calibration(id, 0, 15 * 60000));
    rows = display_rows(registry, false);
    CHECK(rows[0].status == "CAL");
    CHECK(rows[0].value == "20.0\xC2\xB0"
                           "C"); // the reading still shows
    CHECK(registry.end_calibration_hold(id));
    rows = display_rows(registry, false);
    CHECK(rows[0].status.empty());
}

int main() {
    hold_needs_a_connected_probe();
    held_probe_raises_no_high_alarm();
    starting_a_hold_clears_an_active_high_alarm();
    high_alarm_returns_after_the_hold_with_its_persistence_time();
    disconnect_still_alarms_during_a_hold();
    held_readings_are_not_recorded();
    hold_ends_on_its_own_and_renewal_replaces_it();
    a_hold_is_capped_at_twenty_minutes();
    ending_a_hold_early_clears_it();
    retiring_a_probe_ends_its_hold();
    lcd_shows_cal_while_held();
    std::cout << "calibration hold tests passed\n";
}
