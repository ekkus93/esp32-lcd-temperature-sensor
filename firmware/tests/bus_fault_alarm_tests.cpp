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
uint64_t rom_id() {
    std::array<uint8_t, 7> bytes{0x28, 1, 2, 3, 4, 5, 6};
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes.size(); ++i)
        value |= uint64_t(bytes[i]) << (8 * i);
    return value | (uint64_t(crc8(bytes.data(), bytes.size())) << 56);
}
size_t sensor_events(const Registry &registry, uint64_t id) {
    const auto &events = registry.events();
    return std::count_if(events.begin(), events.end(),
                         [id](const Event &event) { return event.rom == id; });
}
const Sensor &sensor(const Registry &registry) {
    return registry.sensors().front();
}
// A probe with a 30 degree high limit, 0.5 hysteresis and 15 s persistence.
Registry limited_registry(uint64_t id) {
    Registry registry;
    CHECK(registry.discover(id));
    auto settings = sensor(registry).settings;
    settings.high = 30;
    settings.hysteresis = 0.5;
    settings.persistence_ms = 15000;
    CHECK(registry.configure(id, settings));
    return registry;
}
// Samples every 5 s from `from` until the probe is in `wanted`; returns the next sample time.
int64_t sample_until(Registry &registry, uint64_t id, double celsius, State wanted, int64_t from) {
    int64_t now = from;
    for (int i = 0; i < 12 && sensor(registry).state != wanted; ++i, now += sample_ms)
        registry.sample({{id, celsius}}, now);
    CHECK(sensor(registry).state == wanted);
    return now;
}
} // namespace
int main() {
    const auto id = rom_id();

    // High -> bus fault -> recovery while still over the limit: straight back to high with no
    // persistence wait and no new alarm event for the probe (the alarm episode never ended).
    {
        auto registry = limited_registry(id);
        auto now = sample_until(registry, id, 35, State::high, 0);
        const auto before = sensor_events(registry, id);
        registry.sample({}, now, true);
        CHECK(sensor(registry).state == State::bus_fault && !sensor(registry).temperature);
        now += sample_ms;
        registry.sample({{id, 35}}, now, false);
        CHECK(sensor(registry).state == State::high);
        CHECK(sensor(registry).temperature && *sensor(registry).temperature == 35);
        CHECK(sensor_events(registry, id) == before);
    }

    // High -> bus fault -> recovery inside the hysteresis band: still high, as without the fault.
    {
        auto registry = limited_registry(id);
        auto now = sample_until(registry, id, 35, State::high, 0);
        registry.sample({}, now, true);
        registry.sample({{id, 29.8}}, now + sample_ms, false);
        CHECK(sensor(registry).state == State::high);
    }

    // High -> bus fault -> recovery below the limit: one clearing event, previous state high.
    {
        auto registry = limited_registry(id);
        auto now = sample_until(registry, id, 35, State::high, 0);
        const auto before = sensor_events(registry, id);
        registry.sample({}, now, true);
        registry.sample({{id, 20}}, now + sample_ms, false);
        CHECK(sensor(registry).state == State::normal);
        CHECK(sensor_events(registry, id) == before + 1);
        bool found = false;
        for (const auto &event : registry.events())
            if (event.rom == id && event.previous == State::high && event.state == State::normal)
                found = true;
        CHECK(found);
    }

    // Normal -> bus fault -> recovery over the limit: a new alarm still has to persist.
    {
        auto registry = limited_registry(id);
        registry.sample({{id, 20}}, 0);
        registry.sample({}, sample_ms, true);
        registry.sample({{id, 35}}, 2 * sample_ms, false);
        CHECK(sensor(registry).state == State::normal);
        auto now = sample_until(registry, id, 35, State::high, 3 * sample_ms);
        CHECK(now >= 2 * sample_ms + 15000);
    }

    // Disconnected -> bus fault -> recovery without a reading: still disconnected, no second
    // raise; the probe coming back clears it once.
    {
        auto registry = limited_registry(id);
        registry.sample({{id, 20}}, 0);
        for (int i = 1; i <= 3; ++i)
            registry.sample({}, i * sample_ms);
        CHECK(sensor(registry).state == State::disconnected);
        const auto before = sensor_events(registry, id);
        registry.sample({}, 4 * sample_ms, true);
        CHECK(sensor(registry).state == State::bus_fault);
        registry.sample({}, 5 * sample_ms, false);
        CHECK(sensor(registry).state == State::disconnected);
        CHECK(sensor_events(registry, id) == before);
        registry.sample({{id, 20}}, 6 * sample_ms, false);
        CHECK(sensor(registry).state == State::normal);
        CHECK(sensor_events(registry, id) == before + 1);
    }

    // The bus-level fault is still recorded once per fault with rom 0.
    {
        auto registry = limited_registry(id);
        registry.sample({{id, 20}}, 0);
        registry.sample({}, sample_ms, true);
        registry.sample({}, 2 * sample_ms, true);
        registry.sample({{id, 20}}, 3 * sample_ms, false);
        size_t bus_events = 0;
        for (const auto &event : registry.events())
            bus_events += event.rom == 0;
        CHECK(bus_events == 2);
    }

    // Disabling a probe during a fault leaves it disabled afterwards.
    {
        auto registry = limited_registry(id);
        auto now = sample_until(registry, id, 35, State::high, 0);
        registry.sample({}, now, true);
        auto settings = sensor(registry).settings;
        settings.enabled = false;
        CHECK(registry.configure(id, settings));
        registry.sample({{id, 35}}, now + sample_ms, false);
        CHECK(sensor(registry).state == State::disabled);
    }
    std::cout << "PASS: alarms survive a bus fault without duplicate events or new persistence\n";
}
