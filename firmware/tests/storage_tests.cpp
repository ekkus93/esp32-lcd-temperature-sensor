#include "fake_bus.hpp"
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
    CHECK(data.size() == 67);
    const std::vector<uint8_t> legacy{
        0x54, 0x4d, 0x48, 0x31, 0x1,  0x0,  0x40, 0x0,  0x7,  0x0,  0x0,  0x0,  0x0,
        0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
        0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x1,  0x0,  0x0,  0x0,  0x28, 0x0,  0x0,
        0x0,  0x0,  0x0,  0x0,  0x1e, 0x75, 0xd8, 0xff, 0xff, 0xe0, 0xb1, 0xff, 0xff,
        0x88, 0x13, 0x0,  0x0,  0x3c, 0x0,  0x0,  0x0,  0xce, 0x4f, 0xb7, 0x28};
    auto old_record = decode_bucket(legacy);
    CHECK(old_record && old_record->sensors[0].average == -10.123 &&
          old_record->sensors[0].name.empty());
    auto named = bucket;
    named.sensors[0].name = "Cold, room \"A\"";
    named.sensors[0].state_flags = uint16_t(1u << unsigned(State::low));
    auto new_record = decode_bucket(encode_bucket(named));
    CHECK(new_record && new_record->sensors[0].name == named.sensors[0].name &&
          new_record->sensors[0].state_flags == named.sensors[0].state_flags);
    auto missing = named;
    missing.sensors[0] = {rom, 0, 0, 0, 0, true, "Cold", uint16_t(1u << unsigned(State::disabled))};
    CHECK(decode_bucket(encode_bucket(missing))->sensors[0].count == 0);
    auto invalid_flags = named;
    invalid_flags.sensors[0].state_flags = 0x200;
    CHECK(encode_bucket(invalid_flags).empty());
    Bucket largest{7, 0, {}, {}};
    for (const auto &reading : fake_readings())
        largest.sensors.push_back({reading.rom, 20, 20, 20, 60, false, std::string(32, 'N'), 1});
    CHECK(encode_bucket(largest).size() == max_history_record_bytes);
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
    HistoryStore store(directory, 4 * data.size(), 2 * data.size());
    CHECK(store.recover());
    for (unsigned i = 0; i < 8; ++i) {
        bucket.start_uptime_ms = i * 300000;
        CHECK(store.append(bucket));
    }
    CHECK(store.bytes() <= 4 * data.size());
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
    future[4] = 3;
    auto future_path = std::filesystem::path(directory) / "00000000.tmh";
    {
        std::ofstream file(future_path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(future.data()), future.size());
    }
    CHECK(!store.recover());
    CHECK(std::filesystem::file_size(future_path) == future.size());
    std::filesystem::remove_all(directory);
    CHECK(std::filesystem::create_directory(directory));
    {
        std::ofstream file(future_path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(legacy.data()), legacy.size());
        const auto modern = encode_bucket(named);
        file.write(reinterpret_cast<const char *>(modern.data()), modern.size());
    }
    HistoryStore mixed(directory, 1024, 512);
    CHECK(mixed.recover());
    unsigned mixed_count = 0;
    CHECK(mixed.visit([&](const Bucket &b) {
        CHECK(b.sensors[0].name == (mixed_count ? named.sensors[0].name : ""));
        ++mixed_count;
        return true;
    }));
    CHECK(mixed_count == 2);
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
