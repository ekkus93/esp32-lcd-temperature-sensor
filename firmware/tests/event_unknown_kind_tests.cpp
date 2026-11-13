// A journal written by a newer build can hold event kinds this build does not know. This build
// must still recover that journal (so a rollback works), keep appending, and show only the kinds it
// knows, with paging that neither repeats nor loses a record.
#include "event_store.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

namespace {
JournalEvent event(uint64_t sequence, EventKind kind) {
    JournalEvent value{};
    value.sequence = sequence;
    value.boot_id = 7;
    value.uptime_ms = int64_t(sequence) * 1000;
    value.kind = kind;
    return value;
}

// Writes complete records in order, as a newer build would have left them.
void write_journal(const std::filesystem::path &path, const std::vector<JournalEvent> &events) {
    std::ofstream out(path, std::ios::binary);
    for (const auto &value : events) {
        const auto bytes = encode_event(value);
        CHECK(bytes.size() == event_record_bytes);
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
}

// The kind a future build might write, one this build has never heard of.
constexpr auto future_kind = static_cast<EventKind>(42);
} // namespace

int main() {
    char directory[] = "/tmp/monitor-event-unknown-kind-XXXXXX";
    CHECK(mkdtemp(directory));
    const auto path = std::filesystem::path(directory) / "events.bin";
    write_journal(path, {event(1, EventKind::reboot), event(2, future_kind),
                         event(3, EventKind::wifi_state), event(4, future_kind),
                         event(5, EventKind::clock_sync)});

    // The record of an unknown kind decodes: the reader keeps it rather than rejecting the journal.
    const auto unknown = encode_event(event(2, future_kind));
    const auto decoded = decode_event(unknown);
    CHECK(decoded && static_cast<unsigned>(decoded->kind) == 42);

    // A rollback recovers the whole journal and keeps appending.
    EventStore journal(path.string());
    CHECK(journal.recover());
    auto all = journal.page(0, 50);
    CHECK(all.available);
    CHECK(all.oldest_available == 1);
    // Only the kinds this build knows are shown.
    CHECK(all.events.size() == 3);
    CHECK(all.events[0].sequence == 1 && all.events[1].sequence == 3 &&
          all.events[2].sequence == 5);
    CHECK(all.next_after == 5);
    CHECK(!all.has_more);

    JournalEvent later = event(0, EventKind::storage_error);
    later.uptime_ms = 9000;
    CHECK(journal.append(later));
    auto after = journal.page(5, 50);
    CHECK(after.available && after.events.size() == 1 && after.events[0].sequence == 6);

    // Paging across the unknown records neither repeats nor loses a shown one.
    std::vector<uint64_t> seen;
    uint64_t cursor = 0;
    for (int page = 0; page < 10; ++page) {
        const auto next = journal.page(cursor, 1);
        CHECK(next.available);
        for (const auto &shown : next.events)
            seen.push_back(shown.sequence);
        if (!next.has_more && next.events.empty())
            break;
        CHECK(next.next_after > cursor);
        cursor = next.next_after;
    }
    CHECK((seen == std::vector<uint64_t>{1, 3, 5, 6}));

    // A reader that holds only known kinds never writes an unknown one.
    JournalEvent foreign = event(0, future_kind);
    CHECK(!journal.append(foreign));

    std::filesystem::remove_all(directory);
    std::cout << "unknown event kinds are kept and skipped, not rejected\n";
    return 0;
}
