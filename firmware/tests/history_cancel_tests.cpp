// History work stops at its deadline (review audit F4): a cancelled visit starts no further
// segment, stops within one 4 KiB chunk of reading or hashing, skips the final revalidation and is
// never reported as validated. Ordinary pagination (a visitor returning false) still validates the
// rest.
#include "history_visit.hpp"
#include <chrono>
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
constexpr int64_t utc0 = 1800000000;
constexpr int segments = 6;      // 5 completed and the active one
constexpr int per_segment = 100; // about 7 KiB per segment: two 4 KiB chunks each

uint64_t probe_rom(unsigned serial) {
    uint8_t bytes[7] = {0x28, uint8_t(serial)};
    return 0x28 | uint64_t(serial) << 8 | uint64_t(crc8(bytes, 7)) << 56;
}
Bucket record(int64_t index) {
    Bucket bucket{7, index * five_minutes, utc0 + index * 300, {}};
    for (unsigned serial = 1; serial <= 2; ++serial)
        bucket.sensors.push_back({probe_rom(serial), 4, 3, 5, 60, false});
    return bucket;
}
std::filesystem::path segment(const std::string &directory, unsigned number) {
    char name[16];
    std::snprintf(name, sizeof(name), "%08u.tmh", number);
    return std::filesystem::path(directory) / name;
}
void damage(const std::filesystem::path &file) {
    std::fstream stream(file, std::ios::binary | std::ios::in | std::ios::out);
    stream.seekg(-1, std::ios::end);
    char last = 0;
    stream.get(last);
    stream.seekp(-1, std::ios::end);
    stream.put(static_cast<char>(last ^ 0x5a));
}
const HistoryRange everything{std::nullopt, utc0 * 1000,
                              (utc0 + segments * per_segment * 300) * 1000};

// A fake clock: the deadline passes at the `limit`-th check. Records how many files the visit had
// opened when it did, so the test can prove nothing more was read afterwards.
struct FakeDeadline {
    std::size_t limit;
    std::size_t checks = 0;
    std::size_t files_at_expiry = 0;
    const HistoryVisitControl *control = nullptr;
    bool operator()() {
        if (++checks == limit && control)
            files_at_expiry = control->files_read;
        return checks >= limit;
    }
};
struct Run {
    bool ok = false, cancelled = false;
    std::size_t files = 0, delivered = 0, skipped = 0, checks = 0, files_at_expiry = 0;
};
Run run(const HistoryStore &store, std::size_t limit, bool paginate = false,
        const HistoryRange *range = nullptr) {
    HistoryVisitControl control;
    FakeDeadline deadline{limit};
    deadline.control = &control;
    control.expired = [&]() { return deadline(); };
    Run result;
    result.ok = store.visit(
        [&](const Bucket &) {
            ++result.delivered;
            return !paginate;
        },
        &result.skipped, range, &control);
    result.cancelled = control.cancelled;
    result.files = control.files_read;
    result.checks = deadline.checks;
    result.files_at_expiry = deadline.files_at_expiry;
    return result;
}
} // namespace

int main() {
    char name[] = "/tmp/monitor-history-cancel-XXXXXX";
    CHECK(mkdtemp(name));
    const std::string directory = name;
    const auto bytes = encode_bucket(record(0)).size();
    {
        HistoryStore writer(directory, 64 * per_segment * bytes, per_segment * bytes);
        CHECK(writer.recover());
        for (int i = 0; i < segments * per_segment; ++i)
            CHECK(writer.append(record(i)));
    }
    CHECK(std::filesystem::exists(segment(directory, segments - 1)));
    CHECK(!std::filesystem::exists(segment(directory, segments)));
    CHECK(per_segment * bytes > 4096);
    HistoryStore store(directory, 64 * per_segment * bytes, per_segment * bytes);
    CHECK(store.recover());

    // A complete visit keeps every integrity check: each segment read once, then all revalidated.
    const auto full = run(store, SIZE_MAX);
    CHECK(full.ok && !full.cancelled && full.delivered == segments * per_segment);
    CHECK(full.files == 2 * segments);
    const auto total_checks = full.checks;

    // Cancelled at any point: false, flagged, and not one more file opened after the deadline.
    // Sampled across the whole visit, including the first and last checks (the final revalidation).
    for (std::size_t limit = 1; limit <= total_checks; limit += 1 + total_checks / 97) {
        const auto cancelled = run(store, limit);
        CHECK(!cancelled.ok && cancelled.cancelled);
        CHECK(cancelled.files == cancelled.files_at_expiry);
        CHECK(cancelled.checks == limit); // nothing ran that needed another check
    }
    {
        const auto last = run(store, total_checks);
        CHECK(!last.ok && last.cancelled && last.delivered == segments * per_segment);
    }
    // Deadline during the first snapshot read and hash (check 1 is before the segment, check 2
    // after its first 4 KiB chunk): one file opened, nothing delivered.
    {
        const auto hashing = run(store, 2);
        CHECK(hashing.cancelled && hashing.files == 1 && hashing.delivered == 0);
    }

    // Pagination is not cancellation: delivery stops at the first record, validation goes on.
    {
        const auto page = run(store, SIZE_MAX, true);
        CHECK(page.ok && !page.cancelled && page.delivered == 1 && page.files == 2 * segments);
        // ...and the deadline still bounds that validation, which delivers nothing.
        const auto slow = run(store, full.checks / 2, true);
        CHECK(!slow.ok && slow.cancelled && slow.delivered == 1);
        CHECK(slow.files == slow.files_at_expiry && slow.files < 2 * segments);
    }

    // Out-of-range segments are not read; the deadline is still checked per segment.
    {
        const HistoryRange last_segment{
            std::nullopt, (utc0 + (segments - 1) * per_segment * 300) * 1000, everything.to};
        const auto ranged = run(store, SIZE_MAX, false, &last_segment);
        CHECK(ranged.ok && ranged.delivered == per_segment && ranged.files == 2);
        const auto expired = run(store, 1, false, &last_segment);
        CHECK(expired.cancelled && expired.files == 0);
    }

    // The service-level outcome: a deadline is reported as such, and the chart answer is a valid
    // partial (truncated) one.
    {
        HistoryVisitOptions options;
        std::size_t checks = 0;
        options.expired = [&]() { return ++checks > total_checks / 3; };
        unsigned delivered = 0;
        const auto outcome = visit_history_range(
            store, everything,
            [&](const Bucket &) {
                ++delivered;
                return true;
            },
            options);
        CHECK(outcome.end == HistoryVisitEnd::deadline);
        CHECK(delivered > 0 && delivered < segments * per_segment);
        CHECK(outcome.files_read < 2 * segments);
        HistoryQuery query;
        query.from = everything.from;
        query.to = everything.to;
        query.max_points = 120;
        checks = 0;
        const auto answer = answer_history_query(store, query, options);
        CHECK(answer.ok() && answer.truncated && answer.visit.end == HistoryVisitEnd::deadline);
    }

    // Corruption and a deadline: a damaged completed segment is counted while the visit runs; a
    // later deadline still wins, and nothing is called validated.
    {
        damage(segment(directory, 1));
        const auto skipped = run(store, SIZE_MAX);
        CHECK(skipped.ok && skipped.skipped == 1 &&
              skipped.delivered == (segments - 1) * per_segment);
        const auto late = run(store, skipped.checks - 1);
        CHECK(!late.ok && late.cancelled && late.skipped == 1);
        CHECK(late.files == late.files_at_expiry);
    }

    // Host timing of a full visit of this store (instrumentation for the record, not a device
    // figure).
    {
        const auto started = std::chrono::steady_clock::now();
        const auto timed = run(store, SIZE_MAX);
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        CHECK(timed.ok);
        std::cout << "host full visit: " << segments << " segments, " << timed.checks
                  << " deadline checks, " << elapsed << " us\n";
    }
    std::filesystem::remove_all(directory);
    std::cout << "cancelled history visits stop reading and are never called validated\n";
}
