// A CSV export that is not complete must never end like a complete one (review audit F2). Runs the
// production visit loop and exporter against a real store with damage in each position.
#include "history_csv.hpp"
#include "history_visit.hpp"
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

namespace {
constexpr int64_t five_minutes = 300000;
constexpr int64_t utc0 = 1800000000; // seconds
constexpr int records_per_segment = 4;
constexpr int stored = 16; // segments 0..2 completed, 3 active

uint64_t probe_rom() {
    uint8_t bytes[7] = {0x28};
    return 0x28 | uint64_t(crc8(bytes, 7)) << 56;
}
Bucket record(int64_t index) {
    Bucket bucket{7, index * five_minutes, utc0 + index * 300, {{probe_rom(), 4, 3, 5, 60, false}}};
    bucket.sensors[0].name = "Canopy";
    return bucket;
}
std::filesystem::path segment(const std::string &directory, unsigned number) {
    char name[16];
    std::snprintf(name, sizeof(name), "%08u.tmh", number);
    return std::filesystem::path(directory) / name;
}
// Flips the last byte of a segment in place (same size), so reading it finds the record damaged.
void damage(const std::filesystem::path &file) {
    std::fstream stream(file, std::ios::binary | std::ios::in | std::ios::out);
    stream.seekg(-1, std::ios::end);
    char last = 0;
    stream.get(last);
    stream.seekp(-1, std::ios::end);
    stream.put(static_cast<char>(last ^ 0x5a));
}

// Records what the HTTP response would have carried.
struct RecordingSink final : CsvExportSink {
    bool began = false, finished = false;
    std::string body;
    int sends_before_failure = -1; // < 0: never fail
    bool begin_fails = false;
    bool begin() override {
        began = true;
        if (begin_fails)
            return false;
        body += history_csv_header();
        return true;
    }
    bool send(const char *data, std::size_t size) override {
        if (sends_before_failure == 0)
            return false;
        if (sends_before_failure > 0)
            --sends_before_failure;
        body.append(data, size);
        return true;
    }
    bool finish() override {
        finished = true;
        return true;
    }
    std::size_t rows() const {
        std::size_t lines = 0;
        for (char c : body)
            lines += c == '\n';
        return lines ? lines - 1 : 0; // minus the header row
    }
};

struct Fixture {
    std::string directory;
    std::size_t bytes = encode_bucket(record(0)).size();
    Fixture() {
        char name[] = "/tmp/monitor-history-export-XXXXXX";
        CHECK(mkdtemp(name));
        directory = name;
        HistoryStore writer(directory, 64 * bytes, records_per_segment * bytes);
        CHECK(writer.recover());
        for (int i = 0; i < stored; ++i)
            CHECK(writer.append(record(i)));
        CHECK(std::filesystem::exists(segment(directory, 3)));
        CHECK(!std::filesystem::exists(segment(directory, 4)));
    }
    ~Fixture() {
        std::filesystem::remove_all(directory);
    }
    HistoryStore open() const {
        HistoryStore store(directory, 64 * bytes, records_per_segment * bytes);
        CHECK(store.recover());
        return store;
    }
};

const HistoryRange everything{std::nullopt, utc0 * 1000, (utc0 + stored * 300) * 1000};

CsvExportResult run_export(const HistoryStore &store, RecordingSink &sink,
                           const HistoryRange &range = everything, std::size_t chunk = 64) {
    return export_history_csv(
        [&](const std::function<bool(const Bucket &)> &visitor,
            const HistoryVisitOptions &options) {
            return visit_history_range(store, range, visitor, options);
        },
        {}, false, sink, chunk);
}

HistoryQuery chart_query() {
    HistoryQuery query;
    query.from = everything.from;
    query.to = everything.to;
    query.max_points = 120;
    return query;
}
} // namespace

int main() {
    // Healthy: every row, then the normal final chunk.
    {
        Fixture fixture;
        auto store = fixture.open();
        RecordingSink sink;
        const auto result = run_export(store, sink);
        CHECK(result.end == CsvExportEnd::complete && result.visit.complete());
        CHECK(sink.began && sink.finished && sink.rows() == stored);
        // One large chunk gives the same file.
        RecordingSink whole;
        CHECK(run_export(store, whole, everything, 4096).end == CsvExportEnd::complete);
        CHECK(whole.body == sink.body);
    }
    // No matching record: the header row alone, complete.
    {
        Fixture fixture;
        auto store = fixture.open();
        RecordingSink sink;
        const HistoryRange later{std::nullopt, (utc0 + 100000) * 1000, (utc0 + 200000) * 1000};
        const auto result = run_export(store, sink, later);
        CHECK(result.end == CsvExportEnd::complete);
        CHECK(sink.began && sink.finished && sink.rows() == 0);
    }
    // Damage in the first completed segment is known before any row: refused, nothing sent.
    {
        Fixture fixture;
        auto store = fixture.open();
        damage(segment(fixture.directory, 0));
        RecordingSink sink;
        const auto result = run_export(store, sink);
        CHECK(result.end == CsvExportEnd::refused);
        CHECK(result.visit.end == HistoryVisitEnd::skipped_damaged);
        CHECK(result.visit.skipped_segments == 1);
        CHECK(!sink.began && !sink.finished && sink.body.empty());
        // The damaged file stays on disk as evidence.
        CHECK(std::filesystem::file_size(segment(fixture.directory, 0)) ==
              records_per_segment * fixture.bytes);
    }
    // Damage in a middle or the last completed segment: rows were sent, so the export is aborted
    // without the final chunk, and it stops at the damage rather than sending later rows.
    for (unsigned damaged : {1u, 2u}) {
        Fixture fixture;
        auto store = fixture.open();
        damage(segment(fixture.directory, damaged));
        RecordingSink sink;
        const auto result = run_export(store, sink);
        CHECK(result.end == CsvExportEnd::aborted);
        CHECK(result.visit.end == HistoryVisitEnd::skipped_damaged);
        CHECK(result.visit.skipped_segments == 1);
        CHECK(sink.began && !sink.finished);
        CHECK(sink.rows() <= damaged * records_per_segment);
    }
    // Damage in the last completed segment with nothing in range after it: the damage is only
    // known at the end, after every row was sent. Still no final chunk.
    {
        Fixture fixture;
        auto store = fixture.open();
        damage(segment(fixture.directory, 2));
        RecordingSink sink;
        // Records 0..11: segments 0..2 are read, the active segment 3 is left unread by the index.
        const HistoryRange through_damage{std::nullopt, utc0 * 1000, (utc0 + 12 * 300) * 1000};
        const auto result = run_export(store, sink, through_damage);
        CHECK(result.end == CsvExportEnd::aborted);
        CHECK(sink.began && !sink.finished && sink.rows() == 8);
        // A window that does not cover the damaged segment exports completely.
        const HistoryRange first_two{std::nullopt, utc0 * 1000, (utc0 + 8 * 300) * 1000};
        RecordingSink clean;
        CHECK(run_export(store, clean, first_two).end == CsvExportEnd::complete);
        CHECK(clean.finished && clean.rows() == 8);
    }
    // A damaged active segment fails the store's visit: aborted after the earlier rows.
    {
        Fixture fixture;
        auto store = fixture.open();
        damage(segment(fixture.directory, 3));
        RecordingSink sink;
        const auto result = run_export(store, sink);
        CHECK(result.end == CsvExportEnd::aborted);
        CHECK(result.visit.end == HistoryVisitEnd::storage_failed);
        CHECK(sink.began && !sink.finished);
    }
    // The client goes away mid-export, or before the headers: no final chunk either way.
    {
        Fixture fixture;
        auto store = fixture.open();
        RecordingSink sink;
        sink.sends_before_failure = 1;
        auto result = run_export(store, sink);
        CHECK(result.end == CsvExportEnd::aborted);
        CHECK(result.visit.end == HistoryVisitEnd::delivery_stopped);
        CHECK(!sink.finished);
        RecordingSink closed;
        closed.begin_fails = true;
        result = run_export(store, closed);
        CHECK(result.end == CsvExportEnd::aborted && !closed.finished);
        // The last chunk fails after every row was handed over.
        RecordingSink late;
        late.sends_before_failure = 0;
        result = run_export(store, late, everything, 1 << 20);
        CHECK(result.end == CsvExportEnd::aborted && !late.finished);
    }
    // The service's budget: a visit past its deadline is incomplete, not complete.
    {
        Fixture fixture;
        auto store = fixture.open();
        int checks = 0;
        HistoryVisitOptions options;
        options.expired = [&]() { return ++checks > 5; };
        unsigned delivered = 0;
        const auto outcome = visit_history_range(
            store, everything,
            [&](const Bucket &) {
                ++delivered;
                return true;
            },
            options);
        CHECK(outcome.end == HistoryVisitEnd::deadline && delivered == 5);
    }
    // The chart answer for the same damaged data stays valid and is marked truncated.
    {
        Fixture fixture;
        auto store = fixture.open();
        damage(segment(fixture.directory, 1));
        const auto answer = answer_history_query(store, chart_query());
        CHECK(answer.ok() && answer.truncated);
        CHECK(answer.visit.end == HistoryVisitEnd::skipped_damaged);
        CHECK(!answer.points.empty());
        const auto healthy = answer_history_query(Fixture().open(), chart_query());
        CHECK(healthy.ok() && !healthy.truncated && healthy.visit.complete());
        // A damaged active segment fails the chart query, as before.
        damage(segment(fixture.directory, 3));
        CHECK(!answer_history_query(store, chart_query()).ok());
    }
    std::cout << "incomplete CSV exports never end with the final chunk\n";
}
