// An older build reading a journal the current build wrote with calibration records (review audit
// H09). Built against the verbatim older event_store sources in this directory.
#include "event_store.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

int main(int argc, char **argv) {
    CHECK(argc == 2);
    const std::filesystem::path path = argv[1];
    EventStore journal(path.string());
#if OLDER_READER_TOLERANT
    // Recovers the whole journal, shows the known records in order and skips the calibration
    // ones, then keeps appending after them.
    CHECK(journal.recover());
    const auto page = journal.page(0, 50);
    CHECK(page.available && !page.has_more);
    CHECK(page.events.size() == 5);
    const EventKind expected[] = {EventKind::reboot, EventKind::sensor_state,
                                  EventKind::sensor_state, EventKind::wifi_state,
                                  EventKind::clock_sync};
    for (std::size_t i = 0; i < 5; ++i)
        CHECK(page.events[i].kind == expected[i]);
    CHECK(page.events[2].sequence > page.events[1].sequence + 1); // a skipped record between
    // Paging one at a time neither repeats nor loses a shown record.
    uint64_t cursor = 0;
    for (std::size_t i = 0; i < 5; ++i) {
        const auto next = journal.page(cursor, 1);
        CHECK(next.events.size() == 1 && next.events[0].kind == expected[i]);
        cursor = next.next_after;
    }
    CHECK(journal.page(cursor, 1).events.empty());
    JournalEvent later{};
    later.boot_id = 8;
    later.uptime_ms = 1000;
    later.kind = EventKind::reboot;
    CHECK(journal.append(later));
    EventStore reopened(path.string());
    CHECK(reopened.recover());
    const auto after = reopened.page(0, 50);
    CHECK(after.events.size() == 6 && after.events.back().boot_id == 8);
    std::cout << "the older tolerant reader skips calibration records and keeps working\n";
#else
    // The strict reader refuses the journal: a rollback that far after calibrating loses the
    // journal until it is cleared. Recorded as a limitation, not hidden.
    const auto size = std::filesystem::file_size(path);
    CHECK(!journal.recover());
    // It does not cut or rewrite the journal, so the records survive a later roll-forward.
    CHECK(std::filesystem::file_size(path) == size);
    JournalEvent later{};
    later.boot_id = 8;
    later.uptime_ms = 1000;
    later.kind = EventKind::reboot;
    CHECK(!journal.append(later));
    CHECK(std::filesystem::file_size(path) == size);
    std::cout
        << "the strict pre-3c1a01d reader refuses calibration records (known rollback limit)\n";
#endif
    std::filesystem::remove_all(path.parent_path());
}
