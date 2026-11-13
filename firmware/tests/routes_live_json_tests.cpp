// Live JSON over fakes (God file refactor G11): the snapshot, sensor entries, event kinds and boot
// ids.
#include "api_responses.hpp"
#include "fake_ports.hpp"
#include "live_json.hpp"
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
uint64_t probe(unsigned serial) {
    uint8_t bytes[7] = {0x28, uint8_t(serial)};
    return 0x28 | uint64_t(serial) << 8 | uint64_t(crc8(bytes, 7)) << 56;
}
} // namespace

int main() {
    // Every journal kind has its wire name; boot ids are 16 lowercase hex digits.
    const std::pair<EventKind, const char *> kinds[] = {
        {EventKind::sensor_state, "sensor_state"},
        {EventKind::wifi_state, "wifi_state"},
        {EventKind::clock_sync, "clock_sync"},
        {EventKind::reboot, "reboot"},
        {EventKind::storage_error, "storage_error"},
        {EventKind::calibration_started, "calibration_started"},
        {EventKind::calibration_ended, "calibration_ended"}};
    for (const auto &[kind, name] : kinds)
        CHECK(std::string(event_kind_name(kind)) == name);
    CHECK(boot_text(0xabc) == "0000000000000abc");

    FakeDevice device;
    // Busy published state: no snapshot.
    device.live.busy = true;
    CHECK(snapshot_json(device.ports).empty());
    device.live.busy = false;

    // Three probes; display order 1, 0, 0 (ties by ROM).
    Registry registry;
    for (unsigned serial : {3u, 1u, 2u})
        CHECK(registry.discover(probe(serial)));
    auto settings = registry.sensors()[0].settings; // probe 3
    settings.order = 1;
    CHECK(registry.configure(probe(3), settings));
    for (unsigned serial : {1u, 2u}) {
        auto other = registry.sensors()[serial == 1 ? 1 : 2].settings;
        other.order = 0;
        CHECK(registry.configure(probe(serial), other));
    }
    registry.sample({{probe(1), 20.5}}, 5000);
    device.live.view.registry = registry;
    device.live.view.uptime_ms = 5000;
    device.live.view.logging = true;
    device.network.mdns.clear(); // mDNS unavailable

    const auto unsynced = snapshot_json(device.ports);
    CHECK(unsynced.rfind(R"({"version":1,"sequence":)", 0) == 0);
    CHECK(unsynced.find(R"("boot_id":"0123456789abcdef","uptime_ms":5000,"simulated":false)") !=
          std::string::npos);
    CHECK(unsynced.find(
              R"("firmware":{"version":"0.2.0","partition":"ota_0","ota_state":"valid"})") !=
          std::string::npos);
    CHECK(unsynced.find(R"("clock":{"synchronized":false,"utc":null})") != std::string::npos);
    CHECK(unsynced.find(R"("wifi":{"state":"connected","mdns_hostname":null})") !=
          std::string::npos);
    CHECK(unsynced.find(
              R"("logging":{"healthy":true,"interval_seconds":300,"storage_available":true,)"
              R"("storage_total_bytes":3145728,"storage_used_bytes":1024})") != std::string::npos);
    // Sensors in display order, then by ROM.
    const auto p1 = unsynced.find(rom_hex(probe(1))), p2 = unsynced.find(rom_hex(probe(2))),
               p3 = unsynced.find(rom_hex(probe(3)));
    CHECK(p1 < p2 && p2 < p3);
    CHECK(unsynced.find(R"("temperature_c":20.5,"last_seen_ms":5000)") != std::string::npos);
    CHECK(unsynced.find(R"("temperature_c":null,"last_seen_ms":null)") != std::string::npos);
    CHECK(unsynced.find(R"("calibrating":false,"calibration_hold_ends_uptime_ms":null)") !=
          std::string::npos);

    device.live.view.synchronized = true;
    device.live.view.utc = "2027-01-15T08:05:00Z";
    device.network.mdns = "garage";
    device.live.is_simulated = true;
    const auto synced = snapshot_json(device.ports);
    CHECK(synced.find(R"("clock":{"synchronized":true,"utc":"2027-01-15T08:05:00Z"})") !=
          std::string::npos);
    CHECK(synced.find(R"("mdns_hostname":"garage")") != std::string::npos);
    CHECK(synced.find(R"("simulated":true)") != std::string::npos);

    // A sensor entry carries its ETag for this boot and a held probe's hold end.
    auto held = registry;
    CHECK(held.hold_calibration(probe(1), 5000, 600000));
    const auto &sensor = held.sensors()[1];
    CHECK(sensor.rom == probe(1));
    Json item(sensor_json(sensor, "0123456789abcdef"), cJSON_Delete);
    char *text = cJSON_PrintUnformatted(item.get());
    const std::string entry = text;
    cJSON_free(text);
    CHECK(entry.find("\"etag\":\"\\\"0123456789abcdef:" + rom_hex(probe(1)) + ":") !=
          std::string::npos);
    CHECK(entry.find(R"("calibrating":true,"calibration_hold_ends_uptime_ms":605000)") !=
          std::string::npos);
    std::cout << "live JSON: snapshot, sensor entries, event kinds and boot ids\n";
}
