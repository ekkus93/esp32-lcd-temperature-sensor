#include "fake_bus.hpp"
#include "history_csv.hpp"
#include "monitor.hpp"
#include <cstdlib>
#include <iostream>
// Fake time and bus use the same domain logic as firmware. No physical sensors are required.
int main(int argc, char **argv) {
    if (argc > 1 && std::string(argv[1]) == "--csv-header") {
        std::cout << monitor::history_csv_header();
        return 0;
    }
    unsigned count = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 5;
    if (count > monitor::max_sensors)
        return 2;
    monitor::Registry registry;
    auto readings = monitor::fake_readings(count);
    registry.sample(readings, 0);
    std::cout << "{\"version\":1,\"boot_id\":\"simulator\",\"sequence\":1,\"simulated\":true,"
                 "\"uptime_ms\":0,\"bus_fault\":false,"
                 "\"clock\":{\"utc\":null,\"synchronized\":false},\"wifi\":{\"state\":\"offline\"},"
                 "\"firmware\":{\"version\":\"0.1.0\",\"partition\":\"ota_0\","
                 "\"ota_state\":\"valid\"},"
                 "\"logging\":{\"healthy\":false,\"interval_seconds\":300,"
                 "\"storage_available\":false,\"storage_total_bytes\":0,"
                 "\"storage_used_bytes\":0},\"sensors\":[";
    bool first = true;
    for (const auto &sensor : registry.sensors()) {
        if (!first)
            std::cout << ',';
        first = false;
        std::cout << "{\"rom_id\":\"" << monitor::rom_hex(sensor.rom) << "\",\"name\":\""
                  << sensor.settings.name << "\",\"temperature_c\":" << *sensor.temperature
                  << ",\"state\":\"" << monitor::state_name(sensor.state)
                  << "\",\"last_seen_ms\":0,\"enabled\":true,\"order\":" << sensor.settings.order
                  << '}';
    }
    std::cout << "]}\n";
}
