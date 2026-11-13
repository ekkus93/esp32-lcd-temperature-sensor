// Writes, with the current EventStore, a journal holding calibration start/end records between
// known ones, in a fresh temporary directory, and prints the journal's path (review audit H09).
#include "event_store.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>
using namespace monitor;

namespace {
uint64_t probe_rom() {
    uint8_t bytes[7] = {0x28, 1};
    return 0x28 | uint64_t(1) << 8 | uint64_t(crc8(bytes, 7)) << 56;
}
} // namespace

int main() {
    char directory[] = "/tmp/monitor-journal-compat-XXXXXX";
    if (!mkdtemp(directory))
        return 1;
    const std::string path = std::string(directory) + "/events.bin";
    EventStore journal(path);
    if (!journal.recover())
        return 1;
    const EventKind kinds[] = {
        EventKind::reboot,       EventKind::sensor_state,      EventKind::calibration_started,
        EventKind::sensor_state, EventKind::calibration_ended, EventKind::wifi_state,
        EventKind::clock_sync};
    int64_t uptime = 0;
    for (const auto kind : kinds) {
        JournalEvent event{};
        event.boot_id = 7;
        uptime += 5000; // far enough apart that nothing is merged
        event.uptime_ms = uptime;
        event.kind = kind;
        event.rom = kind == EventKind::reboot || kind == EventKind::wifi_state ||
                            kind == EventKind::clock_sync
                        ? 0
                        : probe_rom();
        event.state = kind == EventKind::sensor_state ? State::low : State::normal;
        if (!journal.append(event)) {
            std::cerr << "append failed for kind " << unsigned(kind) << '\n';
            return 1;
        }
    }
    const auto page = journal.page(0, 50);
    if (page.events.size() != 7)
        return 1;
    std::cout << path << '\n';
}
