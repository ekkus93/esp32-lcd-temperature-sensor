#include "history.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    uint64_t rom = 0x28;
    uint8_t bytes[7] = {0x28};
    rom |= uint64_t(crc8(bytes, 7)) << 56;
    Bucket bucket{7, 0, std::nullopt, {{rom, -10.123, -20, 5, 60, false}}};
    auto data = encode_bucket(bucket);
    CHECK(data.size() == 64);
    auto restored = decode_bucket(data);
    CHECK(restored && restored->sensors[0].average == -10.123 && !restored->utc_seconds);
    for (std::size_t i = 0; i < data.size(); ++i) {
        auto corrupt = data;
        corrupt[i] ^= 1;
        CHECK(!decode_bucket(corrupt));
    }
    for (std::size_t i = 0; i < data.size(); ++i)
        CHECK(!decode_bucket(std::vector<uint8_t>(data.begin(), data.begin() + i)));
    auto duplicate = bucket;
    duplicate.sensors.push_back(bucket.sensors[0]);
    CHECK(encode_bucket(duplicate).empty());
    char directory[] = "/tmp/monitor-history-XXXXXX";
    CHECK(mkdtemp(directory));
    HistoryStore store(directory, 256, 128);
    CHECK(store.recover());
    for (unsigned i = 0; i < 8; ++i) {
        bucket.start_uptime_ms = i * 300000;
        CHECK(store.append(bucket));
    }
    CHECK(store.bytes() <= 256);
    unsigned count = 0;
    int64_t first = -1;
    CHECK(store.visit([&](const Bucket &b) {
        if (first < 0)
            first = b.start_uptime_ms;
        ++count;
        return true;
    }));
    CHECK(count == 4 && first == 4 * 300000);
    auto last = std::filesystem::path(directory) / "00000003.tmh";
    {
        std::ofstream corrupt(last, std::ios::binary | std::ios::app);
        corrupt << "partial write";
    }
    CHECK(!store.visit([](const Bucket &) { return true; }));
    CHECK(store.recover());
    count = 0;
    CHECK(store.visit([&](const Bucket &) {
        ++count;
        return true;
    }));
    CHECK(count == 4);
    // Corruption before a later valid record is not a trailing interrupted write.
    {
        std::fstream corrupt(last, std::ios::binary | std::ios::out | std::ios::in);
        corrupt.put('X');
    }
    auto active_size = std::filesystem::file_size(last);
    CHECK(!store.recover());
    CHECK(std::filesystem::file_size(last) == active_size);
    {
        std::fstream repair(last, std::ios::binary | std::ios::out | std::ios::in);
        repair.put('T');
    }
    // Damage a completed segment: fail without rewriting it or erasing unrelated history.
    auto old = std::filesystem::path(directory) / "00000002.tmh";
    {
        std::fstream corrupt(old, std::ios::binary | std::ios::out | std::ios::in);
        corrupt.put('X');
    }
    auto size = std::filesystem::file_size(old);
    CHECK(!store.recover());
    CHECK(std::filesystem::file_size(old) == size);
    std::filesystem::remove_all(directory);
    CHECK(std::filesystem::create_directory(directory));
    auto future = data;
    future[4] = 2;
    auto future_path = std::filesystem::path(directory) / "00000000.tmh";
    {
        std::ofstream file(future_path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(future.data()), future.size());
    }
    CHECK(!store.recover());
    CHECK(std::filesystem::file_size(future_path) == future.size());
    std::filesystem::remove_all(directory);
    Registry registry;
    Aggregator aggregation(123);
    unsigned windows = 0;
    // Accelerated 24-hour run. Raw 5-second samples never become individual flash records.
    for (int64_t now = 0; now <= 86400000; now += sample_ms) {
        registry.sample({{rom, 20.0}}, now);
        auto result = aggregation.add(registry, now);
        if (result) {
            ++windows;
            CHECK(result->sensors[0].count == 60);
            CHECK(!result->sensors[0].partial);
        }
    }
    CHECK(windows == 288);
    std::cout
        << "PASS: binary CRC, truncation, rotation, retention, recovery and 24-hour aggregation\n";
}
