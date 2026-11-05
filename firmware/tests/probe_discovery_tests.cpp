#include "fake_bus.hpp"
#include "monitor.hpp"
#include "probe_discovery.hpp"
#include <algorithm>
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

static uint64_t rom_with_serial(uint8_t serial) {
    uint8_t bytes[8]{0x28, serial};
    bytes[7] = crc8(bytes, 7);
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= uint64_t(bytes[i]) << (i * 8);
    return value;
}

int main() {
    auto managed_readings = fake_readings(max_sensors);
    const auto replacement_readings = fake_replacement_readings();
    CHECK(replacement_readings.size() == max_sensors);
    CHECK(replacement_readings.front().rom == managed_readings[1].rom);
    CHECK(replacement_readings.back().rom == rom_with_serial(6));
    std::vector<uint64_t> managed;
    for (const auto &reading : managed_readings)
        managed.push_back(reading.rom);
    const auto lower_sorting_new_probe = rom_with_serial(0);
    CHECK(lower_sorting_new_probe < *std::min_element(managed.begin(), managed.end()));

    for (unsigned count = 0; count <= 6; ++count) {
        auto discovered = managed;
        if (count == 0)
            discovered.clear();
        else if (count < max_sensors)
            discovered.resize(count);
        else if (count == 6)
            discovered.push_back(lower_sorting_new_probe);
        const auto status = probe_scan_status(discovered.size(), true, false);
        CHECK(status.discovered_count == count);
        CHECK(status.overflow == (count > max_sensors));
        CHECK(status.complete);
        const auto selected =
            select_probe_reads(discovered, count == 6 ? managed : std::vector<uint64_t>{});
        CHECK(selected.size() == std::min<unsigned>(count, max_sensors));
    }

    std::vector<uint64_t> six{lower_sorting_new_probe};
    six.insert(six.end(), managed.begin(), managed.end());
    auto raw_scan = six;
    raw_scan.push_back(managed[0]);
    raw_scan.push_back(0x1234); // A valid CRC from another 1-Wire family is ignored.
    const auto collected = collect_probe_discovery(raw_scan, true, false);
    CHECK(collected.status.complete && collected.status.discovered_count == 6 &&
          collected.status.overflow && collected.roms.size() == 6 &&
          collected.status.discovered_roms == collected.roms);
    Registry registry;
    registry.sample(managed_readings, 0);
    const auto physical_selection = select_probe_reads(collected.roms, managed);
    std::vector<Reading> simulated_transport_readings;
    for (const auto rom : physical_selection)
        simulated_transport_readings.push_back(
            {rom, rom == lower_sorting_new_probe ? 30.0 : 20.0, true});
    registry.sample(simulated_transport_readings, sample_ms);
    CHECK(registry.sensors().size() == max_sensors);
    for (const auto &sensor : registry.sensors())
        CHECK(sensor.temperature && *sensor.temperature == 20.0 && sensor.state == State::normal);
    for (unsigned rotation = 0; rotation < six.size(); ++rotation) {
        std::rotate(six.begin(), six.begin() + 1, six.end());
        const auto fake_transport = collect_probe_discovery(six, true, false);
        auto selected = select_probe_reads(fake_transport.roms, managed);
        CHECK(selected.size() == max_sensors);
        CHECK(std::all_of(managed.begin(), managed.end(), [&](auto rom) {
            return std::find(selected.begin(), selected.end(), rom) != selected.end();
        }));
    }

    const auto deduplicated = select_probe_reads(
        {managed[0], managed[0], 0, lower_sorting_new_probe}, {managed[0], managed[0]});
    CHECK(deduplicated.size() == 2 && deduplicated[0] == managed[0] &&
          deduplicated[1] == lower_sorting_new_probe);
    auto capped = probe_scan_status(16, false, true);
    CHECK(capped.discovered_count == 16 && capped.overflow && !capped.complete);
    std::vector<uint64_t> cap_observations;
    for (uint8_t serial = 0; serial < 17; ++serial)
        cap_observations.push_back(rom_with_serial(serial));
    const auto cap_result = collect_probe_discovery(cap_observations, false, true);
    CHECK(cap_result.roms.size() == 16 && cap_result.status.discovered_count == 16 &&
          cap_result.status.overflow && !cap_result.status.complete);
    auto failed = probe_scan_status(3, false, false);
    CHECK(failed.discovered_count == 3 && !failed.overflow && !failed.complete);
    auto cleared = probe_scan_status(4, true, false);
    CHECK(cleared.discovered_count == 4 && !cleared.overflow && cleared.complete);

    std::cout << "PASS: bounded discovery counts, overflow reset, scan incompleteness, "
                 "managed-read priority and ROM-order permutations\n";
}
