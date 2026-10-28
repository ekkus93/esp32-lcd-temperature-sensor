#include "fake_bus.hpp"
#include "history.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

int main() {
    const auto probes = fake_readings();
    char root[] = "/tmp/monitor-power-cut-XXXXXX";
    CHECK(mkdtemp(root));
    for (size_t probe_count : {size_t(1), size_t(5)}) {
        Bucket first{123, 0, std::nullopt, {}};
        for (size_t i = 0; i < probe_count; ++i) {
            Aggregate sensor{probes[i].rom, 20, 19, 21, 60, false};
            sensor.name = "Probe " + std::to_string(i);
            first.sensors.push_back(sensor);
        }
        Bucket next = first;
        next.start_uptime_ms = 300000;
        const auto encoded = encode_bucket(next);
        const auto first_bytes = encode_bucket(first);
        CHECK(!encoded.empty() && encoded.size() == first_bytes.size());
        const auto path = std::filesystem::path(root) / "history";

        // A synthetic power loss interrupts a record at every possible byte,
        // including its header, payload, and CRC, both within a segment and
        // immediately after a segment rollover. Restart must preserve the
        // completed record, remove only the incomplete active tail, and append
        // another valid record without introducing a fabricated reading.
        for (const bool rollover : {false, true}) {
            const size_t segment_bytes = rollover ? first_bytes.size() : 4096;
            for (size_t cut = 0; cut < encoded.size(); ++cut) {
                std::filesystem::remove_all(path);
                HistoryStore before(path.string(), 4 * segment_bytes, segment_bytes);
                CHECK(before.recover() && before.append(first));
                if (cut) {
                    const auto file = path / (rollover ? "00000001.tmh" : "00000000.tmh");
                    std::ofstream interrupted(file, std::ios::binary | std::ios::app);
                    CHECK(interrupted.good());
                    interrupted.write(reinterpret_cast<const char *>(encoded.data()), cut);
                    CHECK(interrupted.good());
                }
                HistoryStore after(path.string(), 4 * segment_bytes, segment_bytes);
                CHECK(after.recover());
                CHECK(after.bytes() == first_bytes.size());
                unsigned count = 0;
                CHECK(after.visit([&](const Bucket &record) {
                    CHECK(record.start_uptime_ms == 0 && record.sensors.size() == probe_count);
                    ++count;
                    return true;
                }));
                CHECK(count == 1);
                CHECK(after.append(next));
                HistoryStore restarted(path.string(), 4 * segment_bytes, segment_bytes);
                CHECK(restarted.recover());
                count = 0;
                CHECK(restarted.visit([&](const Bucket &record) {
                    CHECK(record.start_uptime_ms == int64_t(count) * 300000);
                    CHECK(record.sensors.size() == probe_count);
                    ++count;
                    return true;
                }));
                CHECK(count == 2);
            }
        }
    }
    std::filesystem::remove_all(root);
    std::cout << "PASS: every one- and five-probe TMH2 power-cut prefix/rollover recovers "
                 "without data loss\n";
}
