// The main loop's ports build on the host and their fakes record calls in order (G14).
#include "fake_loop_ports.hpp"
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

int main() {
    FakeLoopDevice device;
    auto &ports = device.ports;
    uint64_t rom = 0;
    Settings settings;
    bool retire = false;
    CHECK(!ports.api.next_sensor_update(rom, settings, retire));
    device.api.updates.push_back({7, Settings{}, true});
    CHECK(ports.api.next_sensor_update(rom, settings, retire) && rom == 7 && retire);
    ports.sampling.read(0, {});
    ports.history.submit_event(JournalEvent{});
    ports.api.set_sensor_settings_writable(false);
    ports.api.publish(Registry{}, {}, 0, true, false, "");
    CHECK(ports.display.hand_over(DisplayFrame{}));
    CHECK(ports.trend_restore.start() && !ports.trend_restore.take());
    device.ota.pending = true;
    CHECK(ports.ota.pending_verify() && ports.ota.mark_valid() == ESP_OK &&
          !ports.ota.pending_verify());
    CHECK((device.trace == Trace{"api.take_update", "sampling.read", "history.event",
                                 "api.read_only", "api.publish", "display.hand_over",
                                 "trends.start", "ota.mark_valid"}));
    CHECK(!ports.clock.synchronized());
    device.clock.utc = 1800000000;
    CHECK(ports.clock.synchronized() && ports.clock.trusted_utc() == 1800000000);
    std::cout << "main loop ports build on the host; fakes record calls in order\n";
}
