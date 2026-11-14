// The main loop over fakes (God file refactor G14.2, G15): the order of a cycle's side effects and
// each step's rules, with the real SettingsService and SensorSettingsPersistence over an in-memory
// NVS.
#include "fake_loop_ports.hpp"
#include "monitor_loop.hpp"
#include "nvs.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
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

// In-memory NVS for SettingsService.
namespace {
std::string durable;
bool fail_commit = false;
std::map<nvs_handle_t, std::string> staged;
nvs_handle_t next_handle = 1;
} // namespace
extern "C" esp_err_t nvs_open(const char *, int, nvs_handle_t *out) {
    *out = next_handle++;
    return ESP_OK;
}
extern "C" esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *out, size_t *size) {
    if (durable.empty())
        return ESP_ERR_NVS_NOT_FOUND;
    if (out)
        std::memcpy(out, durable.data(), std::min(*size, durable.size()));
    *size = durable.size();
    return ESP_OK;
}
extern "C" esp_err_t nvs_set_blob(nvs_handle_t handle, const char *, const void *data,
                                  size_t size) {
    staged[handle] = std::string(static_cast<const char *>(data), size);
    return ESP_OK;
}
extern "C" esp_err_t nvs_commit(nvs_handle_t handle) {
    if (fail_commit)
        return ESP_FAIL;
    durable = staged[handle];
    return ESP_OK;
}
extern "C" void nvs_close(nvs_handle_t handle) {
    staged.erase(handle);
}

namespace {
uint64_t probe(unsigned serial) {
    uint8_t bytes[7] = {0x28, uint8_t(serial)};
    return 0x28 | uint64_t(serial) << 8 | uint64_t(crc8(bytes, 7)) << 56;
}
const ProbeMemory factory{0x4b, 0x46};
std::size_t at(const Trace &trace, const std::string &name) {
    return std::find(trace.begin(), trace.end(), name) - trace.begin();
}
std::size_t count(const Trace &trace, const std::string &name) {
    return std::count(trace.begin(), trace.end(), name);
}
// A loop over fakes, with its registry, settings and persistence.
struct Bench {
    FakeLoopDevice device;
    Registry registry;
    SettingsService settings{"test"};
    std::unique_ptr<SensorSettingsPersistence> persistence;
    std::unique_ptr<MonitorLoop> loop;
    explicit Bench(LoopConfig config = {}) {
        durable.clear();
        fail_commit = false;
        settings.load(registry);
        persistence = std::make_unique<SensorSettingsPersistence>(settings);
        device.clock.now = 1000;
        loop = std::make_unique<MonitorLoop>(device.ports, registry, *persistence, config);
    }
    void cycle(int64_t step_ms = 5000) {
        device.trace.clear();
        loop->run_cycle();
        device.clock.now += step_ms;
    }
};
} // namespace

int main() {
    // The order of one cycle's side effects (spec §5.2).
    {
        Bench bench;
        auto &d = bench.device;
        d.sampling.readings = {{probe(1), 21.0, true, factory}};
        bench.cycle(); // discovers the probe
        auto settings = bench.registry.sensors()[0].settings;
        settings.name = "Canopy";
        d.api.updates.push_back({probe(1), settings, false});
        d.api.holds.push_back({probe(1), 60000, false});
        bench.cycle();
        const auto &t = d.trace;
        CHECK(at(t, "api.take_update") < at(t, "api.take_hold"));
        CHECK(at(t, "api.take_hold") < at(t, "sampling.read"));
        CHECK(at(t, "sampling.read") < at(t, "api.writable"));
        CHECK(at(t, "api.writable") < at(t, "api.publish"));
        CHECK(at(t, "api.publish") < at(t, "display.hand_over"));
        CHECK(at(t, "display.hand_over") < t.size());
        CHECK(bench.registry.sensors()[0].settings.name == "Canopy");
        CHECK(bench.registry.sensors()[0].calibration_hold_until_ms.has_value());
        CHECK(d.api.published.back().sensors()[0].settings.name == "Canopy");
    }
    // At most four edits per cycle; the rest wait for the next one.
    {
        Bench bench;
        auto &d = bench.device;
        d.sampling.readings = {{probe(1), 21.0, true, std::nullopt}};
        bench.cycle();
        auto settings = bench.registry.sensors()[0].settings;
        for (int i = 0; i < 6; ++i) {
            settings.name = "Edit " + std::to_string(i);
            d.api.updates.push_back({probe(1), settings, false});
        }
        bench.cycle();
        CHECK(count(d.trace, "api.take_update") == 4 && d.api.updates.size() == 2);
        CHECK(bench.registry.sensors()[0].settings.name == "Edit 3");
        bench.cycle();
        CHECK(bench.registry.sensors()[0].settings.name == "Edit 5");
    }
    // A failed commit: the edit is rolled back and read_only goes out before that publication.
    {
        Bench bench;
        auto &d = bench.device;
        d.sampling.readings = {{probe(1), 21.0, true, std::nullopt}};
        bench.cycle();
        bench.cycle(); // the discovered probe is stored
        fail_commit = true;
        auto settings = bench.registry.sensors()[0].settings;
        settings.name = "Lost";
        d.api.updates.push_back({probe(1), settings, false});
        bench.cycle();
        CHECK(at(d.trace, "api.read_only") < at(d.trace, "api.publish"));
        CHECK(d.api.published.back().sensors()[0].settings.name != "Lost");
        CHECK(d.api.writable == false);
    }
    // Probe memory: at most one write per cycle, none during a bus fault.
    {
        Bench bench;
        auto &d = bench.device;
        d.sampling.readings = {{probe(1), 21.0, true, factory}, {probe(2), 22.0, true, factory}};
        d.sampling.bus_fault = true;
        bench.cycle();
        CHECK(d.sampling.written.empty());
        d.sampling.bus_fault = false;
        bench.cycle();
        CHECK(d.sampling.written.size() == 1);
        // The probe now holds the monitor's offset, and reports it.
        for (auto &reading : d.sampling.readings)
            if (reading.rom == d.sampling.written[0])
                reading.memory = encode_probe_calibration(0);
        bench.cycle();
        CHECK(d.sampling.written.size() == 2 && d.sampling.written[0] != d.sampling.written[1]);
    }
    // Wi-Fi and clock changes are journaled once each; the trend restore starts once, when the
    // clock is trusted, and is applied once.
    {
        Bench bench;
        auto &d = bench.device;
        bench.cycle();
        CHECK(d.history.events.empty());
        d.network.network.state = "offline";
        d.clock.utc = 1800000000;
        d.trend_restore.result = TrendPoints{};
        bench.cycle();
        CHECK(d.history.events.size() == 2);
        CHECK(d.history.events[0].kind == EventKind::wifi_state &&
              d.history.events[0].detail == (2u << 8));
        CHECK(d.history.events[1].kind == EventKind::clock_sync && d.history.events[1].detail == 1);
        CHECK(count(d.trace, "trends.start") == 1);
        bench.cycle();
        CHECK(d.history.events.size() == 2 && count(d.trace, "trends.start") == 0);
    }
    // A five-minute bucket goes to history once its window is complete.
    {
        Bench bench;
        auto &d = bench.device;
        d.sampling.readings = {{probe(1), 21.0, true, std::nullopt}};
        for (int i = 0; i < 70 && d.history.buckets.empty(); ++i)
            bench.cycle();
        CHECK(d.history.buckets.size() == 1);
    }
    // The display: nothing when it is not ready; a refused hand-over is counted as dropped.
    {
        Bench bench;
        auto &d = bench.device;
        d.display.is_ready = false;
        bench.cycle();
        CHECK(count(d.trace, "display.hand_over") == 0);
        d.display.is_ready = true;
        d.display.accepts = false;
        bench.cycle();
        bench.cycle();
        CHECK(bench.loop->display_frames_dropped() == 2);
        d.display.accepts = true;
        d.network.network.provisioning = true;
        bench.cycle();
        CHECK(d.display.frames.back().provisioning &&
              d.display.frames.back().ssid == "TempMonitor-SETUP");
        CHECK(d.display.frames.back().local_name == "temperature-monitor.local");
    }
    // The OTA boot check: only after two cycles, only for a pending image.
    {
        LoopConfig config;
        config.startup = {true, true, true, true, true};
        Bench bench(config);
        auto &d = bench.device;
        d.ota.pending = true;
        bench.cycle();
        CHECK(count(d.trace, "ota.mark_valid") == 0);
        bench.cycle();
        CHECK(count(d.trace, "ota.mark_valid") == 1);
        bench.cycle();
        CHECK(count(d.trace, "ota.mark_valid") == 0); // checked once
        // Startup incomplete: waits, then rolls back after the grace period.
        LoopConfig broken;
        broken.startup = {true, false, true, true, true};
        Bench failing(broken);
        failing.device.ota.pending = true;
        failing.cycle();
        failing.cycle();
        CHECK(count(failing.device.trace, "ota.roll_back") == 0);
        failing.device.clock.now = 10 * 60 * 1000;
        failing.cycle();
        CHECK(count(failing.device.trace, "ota.roll_back") == 1);
        // Not pending: never decided.
        Bench steady(config);
        for (int i = 0; i < 5; ++i)
            steady.cycle();
        CHECK(count(steady.device.trace, "ota.mark_valid") == 0);
    }
    // The simulation: readings at the tick's time, no OTA check, no UTC.
    {
        LoopConfig config;
        config.simulated = true;
        Bench bench(config);
        auto &d = bench.device;
        d.ota.pending = true;
        d.clock.utc = 1800000000;
        d.sampling.readings = {{probe(1), 21.0, true, std::nullopt}};
        for (int i = 0; i < 4; ++i)
            bench.cycle(250);
        CHECK(bench.loop->cycle_tick() == 3);
        CHECK(bench.registry.sensors()[0].last_seen_ms == 15000);
        CHECK(count(d.trace, "ota.mark_valid") == 0 && count(d.trace, "trends.start") == 0);
    }
    std::cout << "main loop: cycle order, edits, rollback, probe memory, journal, display, OTA\n";
}
