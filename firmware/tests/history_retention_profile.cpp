#include "history.hpp"
#include "monitor.hpp"
#include "monitor_types.hpp"
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

using namespace monitor;

namespace {
constexpr std::size_t history_budget = 3 * 1024 * 1024;
constexpr std::size_t segment_size = 32 * 1024;

uint64_t probe(unsigned serial) {
    const uint64_t value = 0x28 | (uint64_t(serial) << 8);
    const uint8_t bytes[7] = {0x28, uint8_t(serial)};
    return value | (uint64_t(crc8(bytes, 7)) << 56);
}

bool profile(unsigned probe_count) {
    std::string pattern = "/tmp/history-retention-XXXXXX";
    char *directory = mkdtemp(pattern.data());
    if (!directory)
        return false;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{directory};

    HistoryStore store(directory, history_budget, segment_size);
    if (!store.recover())
        return false;
    Bucket bucket{1, 0, std::nullopt, {}};
    for (unsigned i = 0; i < probe_count; ++i)
        bucket.sensors.push_back({probe(i + 1), 20, 19, 21, 60, false, std::string(32, 'N'), 1});
    const auto record_bytes = encode_bucket(bucket).size();
    if (record_bytes == 0)
        return false;

    // Write past capacity so the profile measures steady-state segment eviction.
    const uint64_t writes = history_budget / record_bytes + 1000;
    for (uint64_t index = 0; index < writes; ++index) {
        bucket.start_uptime_ms = int64_t(index) * bucket_ms;
        if (!store.append(bucket))
            return false;
    }

    uint64_t rows = 0;
    int64_t oldest_ms = -1;
    int64_t latest_ms = -1;
    bool ordered = true;
    if (!store.visit([&](const Bucket &record) {
            if (oldest_ms < 0)
                oldest_ms = record.start_uptime_ms;
            else if (record.start_uptime_ms != latest_ms + bucket_ms)
                ordered = false;
            latest_ms = record.start_uptime_ms;
            ++rows;
            return true;
        }) ||
        rows == 0)
        return false;
    // Eviction drops whole oldest segments only: what is left is one unbroken run ending with the
    // last record written.
    if (!ordered || latest_ms != int64_t(writes - 1) * bucket_ms ||
        rows != uint64_t((latest_ms - oldest_ms) / bucket_ms) + 1 ||
        store.bytes() > history_budget) {
        std::cerr << "eviction left a gap, lost the newest record or exceeded the budget\n";
        return false;
    }

    std::cout << probe_count << ',' << record_bytes << ',' << store.bytes() << ',' << rows << ','
              << oldest_ms << ',' << latest_ms << ',' << (rows * bucket_ms / 86400000) << std::endl;
    return true;
}
} // namespace

int main(int argc, char **argv) {
    unsigned first = 1;
    unsigned last = 5;
    if (argc == 2) {
        try {
            first = last = static_cast<unsigned>(std::stoul(argv[1]));
        } catch (...) {
            std::cerr << "usage: history_retention_profile [probe-count 1..5]\n";
            return 2;
        }
        if (first < 1 || first > 5) {
            std::cerr << "probe-count must be 1..5\n";
            return 2;
        }
    } else if (argc != 1) {
        std::cerr << "usage: history_retention_profile [probe-count 1..5]\n";
        return 2;
    }

    std::cout << "# host POSIX HistoryStore profile; not LittleFS/device acceptance\n"
              << "probes,record_bytes,retained_file_bytes,records,oldest_start_ms,latest_start_ms,"
                 "retained_days\n";
    for (unsigned count = first; count <= last; ++count)
        if (!profile(count)) {
            std::cerr << "history retention profile failed for " << count << " probes\n";
            return 1;
        }
}
