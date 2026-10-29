#include "fake_bus.hpp"
#include "history.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

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

        // A complete last record with one corrupted byte is not a partial
        // power-cut write. Never erase it to make recovery appear successful,
        // even when no subsequent valid record exists to disambiguate it.
        const auto final_file = path / "00000000.tmh";
        for (size_t index = 0; index < first_bytes.size(); ++index) {
            if (index == 6 || index == 7)
                continue; // Mutated length can mimic a genuinely incomplete tail.
            std::filesystem::remove_all(path);
            HistoryStore before(path.string(), 4 * 4096, 4096);
            CHECK(before.recover() && before.append(first));
            auto damaged = first_bytes;
            damaged[index] ^= 1;
            {
                std::ofstream write(final_file, std::ios::binary | std::ios::trunc);
                CHECK(write.good());
                write.write(reinterpret_cast<const char *>(damaged.data()), damaged.size());
                CHECK(write.good());
            }
            HistoryStore restarted(path.string(), 4 * 4096, 4096);
            CHECK(!restarted.recover());
            CHECK(std::filesystem::file_size(final_file) == damaged.size());
            std::ifstream original(final_file, std::ios::binary);
            const std::vector<uint8_t> preserved((std::istreambuf_iterator<char>(original)),
                                                 std::istreambuf_iterator<char>());
            CHECK(preserved == damaged);
        }
    }
    // Short future-format headers are evidence, not disposable partial
    // TMH2 records. An older firmware image must preserve them on rollback,
    // including prefixes after an otherwise committed bucket.
    const std::vector<std::string> unknown_headers{
        std::string("TMH3"),
        std::string("TMH3") + char(3),
        std::string("TMH2") + char(3),
        std::string("TMH2") + char(2) + char(1),
    };
    Bucket committed{123, 0, std::nullopt, {}};
    committed.sensors.push_back({probes[0].rom, 20, 19, 21, 60, false});
    const auto valid_prefix = encode_bucket(committed);
    CHECK(!valid_prefix.empty());
    const auto path = std::filesystem::path(root) / "history";
    const auto active_file = path / "00000000.tmh";
    for (const auto &unknown : unknown_headers) {
        std::filesystem::remove_all(path);
        HistoryStore before(path.string(), 8192, 4096);
        CHECK(before.recover() && before.append(committed));
        {
            std::ofstream tail(active_file, std::ios::binary | std::ios::app);
            CHECK(tail.good());
            tail.write(unknown.data(), static_cast<std::streamsize>(unknown.size()));
            CHECK(tail.good());
        }
        const auto original_size = valid_prefix.size() + unknown.size();
        HistoryStore restarted(path.string(), 8192, 4096);
        CHECK(!restarted.recover());
        CHECK(std::filesystem::file_size(active_file) == original_size);
        std::ifstream preserved(active_file, std::ios::binary);
        std::vector<uint8_t> persisted;
        char byte = 0;
        while (preserved.get(byte))
            persisted.push_back(static_cast<uint8_t>(byte));
        CHECK(persisted.size() == original_size);
        CHECK(std::equal(valid_prefix.begin(), valid_prefix.end(), persisted.begin()));
        CHECK(std::equal(unknown.begin(), unknown.end(),
                         persisted.begin() + static_cast<std::ptrdiff_t>(valid_prefix.size())));
    }
    // Invalid short headers are not recognizable interrupted TMH writes.
    // Preserve their bytes instead of silently discarding corruption.
    for (size_t prefix = 1; prefix <= 8; ++prefix) {
        std::filesystem::remove_all(path);
        HistoryStore before(path.string(), 8192, 4096);
        CHECK(before.recover() && before.append(committed));
        auto damaged = valid_prefix;
        if (prefix == 7)
            damaged[0] = 'X'; // A partial length byte alone is ambiguous.
        else if (prefix == 8) {
            damaged[6] = 0;
            damaged[7] = 0; // An impossible complete record length.
        } else
            damaged[prefix - 1] ^= 0x80;
        {
            std::ofstream output(active_file, std::ios::binary | std::ios::app);
            CHECK(output.good());
            output.write(reinterpret_cast<const char *>(damaged.data()), prefix);
            CHECK(output.good());
        }
        const auto expected_size = valid_prefix.size() + prefix;
        HistoryStore restarted(path.string(), 8192, 4096);
        CHECK(!restarted.recover());
        CHECK(std::filesystem::file_size(active_file) == expected_size);
        std::ifstream preserved(active_file, std::ios::binary);
        const std::vector<uint8_t> actual((std::istreambuf_iterator<char>(preserved)),
                                          std::istreambuf_iterator<char>());
        CHECK(std::equal(valid_prefix.begin(), valid_prefix.end(), actual.begin()));
        CHECK(std::equal(damaged.begin(), damaged.begin() + prefix,
                         actual.begin() + valid_prefix.size()));
    }
    std::filesystem::remove_all(root);
    std::cout << "PASS: one/five-probe TMH2 interrupted prefixes and complete-record "
                 "corruption preservation\n";
}
