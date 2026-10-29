#include "event_store.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                         \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

namespace {
std::string tamper_path;
std::vector<uint8_t> tamper_bytes;
std::size_t tamper_offset = 0;
bool tamper_next_sync = false;
bool tamper_succeeded = false;
} // namespace

// Linux host-only fault injection: mutate a valid event record *after*
// production EventStore::append has flushed and synced the appended bytes,
// but before that call can acknowledge the new sequence to its caller.
extern "C" int __real_fsync(int fd);
extern "C" int __wrap_fsync(int fd) {
    const int result = __real_fsync(fd);
    if (result != 0 || !tamper_next_sync)
        return result;
    tamper_next_sync = false;
    std::fstream changed(tamper_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!changed)
        return -1;
    changed.seekp(static_cast<std::streamoff>(tamper_offset));
    changed.write(reinterpret_cast<const char *>(tamper_bytes.data()),
                  static_cast<std::streamsize>(tamper_bytes.size()));
    changed.flush();
    tamper_succeeded = changed.good();
    return tamper_succeeded ? result : -1;
}

int main() {
    for (const bool change_newest : {false, true}) {
        char directory[] = "/tmp/monitor-event-post-fsync-XXXXXX";
        CHECK(mkdtemp(directory));
        const auto journal_path = std::filesystem::path(directory) / "events.bin";

        JournalEvent first{};
        first.boot_id = 7;
        first.uptime_ms = 1000;
        first.kind = EventKind::reboot;
        JournalEvent second = first;
        second.uptime_ms = 2000;
        second.kind = EventKind::wifi_state;
        JournalEvent third = first;
        third.uptime_ms = 3000;
        third.kind = EventKind::clock_sync;

        EventStore journal(journal_path.string());
        CHECK(journal.recover());
        CHECK(journal.append(first));
        first.sequence = 1;
        second.sequence = 2;
        const auto original = encode_event(change_newest ? second : first);
        auto changed = change_newest ? second : first;
        changed.kind = EventKind::storage_error;
        const auto replacement = encode_event(changed);
        CHECK(original.size() == event_record_bytes);
        CHECK(replacement.size() == original.size());
        CHECK(decode_event(replacement).has_value());

        tamper_path = journal_path.string();
        tamper_bytes = replacement;
        tamper_offset = change_newest ? event_record_bytes : 0;
        tamper_succeeded = false;
        tamper_next_sync = true;
        second.sequence = 0; // append assigns the durable sequence.
        CHECK(!journal.append(second));
        CHECK(!tamper_next_sync && tamper_succeeded);
        CHECK(journal.bytes() == 2 * event_record_bytes);
        CHECK(!journal.page(0, 50).available);
        CHECK(!journal.append(third));

        {
            const auto read_write = std::ios::binary | std::ios::in | std::ios::out;
            std::fstream repaired(journal_path, read_write);
            CHECK(repaired.good());
            repaired.seekp(static_cast<std::streamoff>(tamper_offset));
            repaired.write(reinterpret_cast<const char *>(original.data()),
                           static_cast<std::streamsize>(original.size()));
            repaired.flush();
            CHECK(repaired.good());
        }
        CHECK(!journal.append(third)); // Repair alone must not clear quarantine.
        CHECK(journal.recover());
        auto page = journal.page(0, 50);
        CHECK(page.available && page.events.size() == 2 && page.next_after == 2);
        CHECK(page.events[0].kind == EventKind::reboot);
        CHECK(page.events[1].kind == EventKind::wifi_state);
        CHECK(journal.append(third));
        page = journal.page(0, 50);
        CHECK(page.available && page.events.size() == 3 && page.next_after == 3);
        std::filesystem::remove_all(directory);
    }
    std::cout << "PASS: event append rejects CRC-valid post-fsync edits and recovers\n";
}
