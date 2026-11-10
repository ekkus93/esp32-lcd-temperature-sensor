// A ranged history visit reads only the segments that can hold records in the requested window.
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

namespace {
constexpr int64_t five_minutes = 300000;
constexpr int64_t utc0 = 1800000000; // seconds

uint64_t probe_rom() {
    uint8_t bytes[7] = {0x28};
    return 0x28 | uint64_t(crc8(bytes, 7)) << 56;
}
Bucket record(uint64_t boot, int64_t index, bool utc) {
    Bucket bucket{boot, index * five_minutes, std::nullopt, {{probe_rom(), 4, 3, 5, 60, false}}};
    if (utc)
        bucket.utc_seconds = utc0 + index * 300;
    return bucket;
}
// Overwrites a record's CRC in place (same size), so reading the segment finds it damaged.
void damage(const std::filesystem::path &segment) {
    std::fstream file(segment, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(-1, std::ios::end);
    char last = 0;
    file.seekg(-1, std::ios::end);
    file.get(last);
    file.seekp(-1, std::ios::end);
    file.put(static_cast<char>(last ^ 0x5a));
}
struct Visit {
    bool ok = false;
    std::size_t skipped = 0;
    unsigned delivered = 0, in_range = 0;
};
Visit run(const HistoryStore &store, const HistoryRange *range) {
    Visit result;
    result.ok = store.visit(
        [&](const Bucket &bucket) {
            ++result.delivered;
            if (range && history_bucket_in_range(bucket, *range))
                ++result.in_range;
            return true;
        },
        &result.skipped, range);
    return result;
}
std::filesystem::path segment(const char *directory, unsigned number) {
    char name[16];
    std::snprintf(name, sizeof(name), "%08u.tmh", number);
    return std::filesystem::path(directory) / name;
}
} // namespace

int main() {
    // The range test itself.
    {
        const auto synced = record(7, 2, true);
        CHECK(history_bucket_in_range(synced,
                                      {std::nullopt, (utc0 + 600) * 1000, (utc0 + 900) * 1000}));
        CHECK(!history_bucket_in_range(synced,
                                       {std::nullopt, (utc0 + 900) * 1000, (utc0 + 1200) * 1000}));
        CHECK(history_bucket_in_range(synced, {7, 2 * five_minutes, 3 * five_minutes}));
        CHECK(!history_bucket_in_range(synced, {8, 0, 10 * five_minutes})); // another boot
        const auto unsynced = record(7, 2, false);
        CHECK(!history_bucket_in_range(unsynced, {std::nullopt, 0, INT64_MAX}));
    }
    char directory[] = "/tmp/monitor-history-range-XXXXXX";
    CHECK(mkdtemp(directory));
    const auto bytes = encode_bucket(record(7, 0, true)).size();
    // Four records per segment; room for the whole test without eviction.
    HistoryStore store(directory, 64 * bytes, 4 * bytes);
    CHECK(store.recover());
    // Boot 7, clock synchronized: records 0..11 in segments 0, 1, 2.
    for (int64_t i = 0; i < 12; ++i)
        CHECK(store.append(record(7, i, true)));
    // Boot 9, clock not synchronized: records 0..3 in segment 3.
    for (int64_t i = 0; i < 4; ++i)
        CHECK(store.append(record(9, i, false)));
    CHECK(std::filesystem::exists(segment(directory, 3)));
    CHECK(!std::filesystem::exists(segment(directory, 4)));

    // Reopen so the index comes from recovery, then damage the first segment in place. Only a
    // visit that reads it can notice.
    HistoryStore reopened(directory, 64 * bytes, 4 * bytes);
    CHECK(reopened.recover());
    damage(segment(directory, 0));

    // UTC window over records 8..11 (segment 2 only): segment 0 is not read.
    const HistoryRange late{std::nullopt, (utc0 + 8 * 300) * 1000, (utc0 + 12 * 300) * 1000};
    auto result = run(reopened, &late);
    CHECK(result.ok && result.skipped == 0);
    CHECK(result.delivered == 4 && result.in_range == 4);

    // Boot 9 by uptime (segment 3 only).
    const HistoryRange boot9{9, 0, 4 * five_minutes};
    result = run(reopened, &boot9);
    CHECK(result.ok && result.skipped == 0);
    CHECK(result.delivered == 4 && result.in_range == 4);

    // A boot that is not stored reads nothing at all.
    const HistoryRange absent{42, 0, INT64_MAX};
    result = run(reopened, &absent);
    CHECK(result.ok && result.delivered == 0);

    // A window that covers the damaged segment reads it, and reports it.
    const HistoryRange early{std::nullopt, utc0 * 1000, (utc0 + 4 * 300) * 1000};
    result = run(reopened, &early);
    CHECK(result.ok && result.skipped == 1 && result.delivered == 0);

    // Without a range every segment is read, as before.
    result = run(reopened, nullptr);
    CHECK(result.ok && result.skipped == 1 && result.delivered == 12);

    // Records appended after recovery extend the index: boot 9 keeps going in segment 4.
    CHECK(reopened.append(record(9, 4, false)));
    const HistoryRange boot9_late{9, 4 * five_minutes, 5 * five_minutes};
    result = run(reopened, &boot9_late);
    CHECK(result.ok && result.skipped == 0 && result.delivered == 1 && result.in_range == 1);

    // A segment whose size no longer matches its index is read rather than trusted: append a
    // valid record for boot 7 to segment 2 behind the store's back.
    {
        std::ofstream extra(segment(directory, 2), std::ios::binary | std::ios::app);
        const auto data = encode_bucket(record(7, 40, true));
        extra.write(reinterpret_cast<const char *>(data.data()),
                    static_cast<std::streamsize>(data.size()));
    }
    const HistoryRange hidden{std::nullopt, (utc0 + 40 * 300) * 1000, (utc0 + 41 * 300) * 1000};
    unsigned found = 0;
    reopened.visit(
        [&](const Bucket &bucket) {
            found += history_bucket_in_range(bucket, hidden);
            return true;
        },
        nullptr, &hidden);
    CHECK(found == 1);
    std::filesystem::remove_all(directory);
    return 0;
}
