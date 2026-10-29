#include "event_store.hpp"
#include "fake_bus.hpp"
#include "history.hpp"
#include "history_csv.hpp"
#include "history_mount_policy.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
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
    std::array<uint8_t, 4096> erased{};
    erased.fill(0xff);
    CHECK(history_partition_erased(erased.data(), erased.size()));
    CHECK(
        may_format_history_partition(true, history_partition_erased(erased.data(), erased.size())));
    CHECK(!may_format_history_partition(false,
                                        history_partition_erased(erased.data(), erased.size())));
    CHECK(!history_partition_erased(nullptr, erased.size()));
    CHECK(!history_partition_erased(erased.data(), 0));
    for (const auto index : {size_t(0), erased.size() / 2, erased.size() - 1}) {
        auto programmed = erased;
        programmed[index] = 0;
        CHECK(!history_partition_erased(programmed.data(), programmed.size()));
        CHECK(!may_format_history_partition(
            true, history_partition_erased(programmed.data(), programmed.size())));
    }
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
    const auto readings = fake_readings();
    for (size_t count = 1; count <= readings.size(); ++count) {
        Bucket measured{7, 0, {}, {}};
        for (size_t index = 0; index < count; ++index)
            measured.sensors.push_back(
                {readings[index].rom, 20, 20, 20, 60, false, std::string(32, 'N'), 1});
        CHECK(encode_bucket(measured).size() == 40 + count * 59);
    }
    auto restored = decode_bucket(data);
    CHECK(restored && restored->sensors[0].average == -10.123 && !restored->utc_seconds);
    // A CRC-valid record with the UTC-present flag cleared must not
    // smuggle a timestamp into a nominally unsynchronized history bucket.
    // Negative UTC timestamps are rejected on both encode and decode.
    auto negative_utc = bucket;
    negative_utc.utc_seconds = -1;
    CHECK(encode_bucket(negative_utc).empty());
    auto synchronized = bucket;
    synchronized.utc_seconds = 1800000000;
    const auto synchronized_bytes = encode_bucket(synchronized);
    CHECK(decode_bucket(synchronized_bytes)->utc_seconds == synchronized.utc_seconds);
    const auto update_crc = [](std::vector<uint8_t> &bytes) {
        uint32_t crc = ~uint32_t{0};
        for (size_t i = 0; i < bytes.size() - 4; ++i) {
            crc ^= bytes[i];
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
        crc = ~crc;
        for (unsigned i = 0; i < 4; ++i)
            bytes[bytes.size() - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    };
    auto hidden_utc = synchronized_bytes;
    hidden_utc[33] = 0;
    update_crc(hidden_utc);
    CHECK(!decode_bucket(hidden_utc));
    auto negative_on_disk = synchronized_bytes;
    std::fill(negative_on_disk.begin() + 24, negative_on_disk.begin() + 32, 0xff);
    update_crc(negative_on_disk);
    CHECK(!decode_bucket(negative_on_disk));
    auto legacy_hidden_utc = legacy;
    legacy_hidden_utc[24] = 1;
    update_crc(legacy_hidden_utc);
    CHECK(!decode_bucket(legacy_hidden_utc));

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
    CHECK(!store.append(bucket)); // Constructor must not bypass recovery.
    CHECK(store.bytes() == 0);
    CHECK(store.recover());
    for (unsigned i = 0; i < 8; ++i) {
        bucket.start_uptime_ms = i * 300000;
        CHECK(store.append(bucket));
    }
    CHECK(store.bytes() <= 4 * data.size());
    // A new writer cannot overwrite or extend existing history before
    // inspecting the persisted segments and selecting the active one.
    HistoryStore unchecked_history(directory, 4 * data.size(), 2 * data.size());
    const auto history_size = store.bytes();
    CHECK(!unchecked_history.append(bucket));
    CHECK(unchecked_history.bytes() == history_size);
    unsigned count = 0;
    int64_t first = -1;
    CHECK(store.visit([&](const Bucket &b) {
        if (first < 0)
            first = b.start_uptime_ms;
        ++count;
        return true;
    }));
    CHECK(count == 4 && first == 4 * 300000);
    char parallel_directory[] = "/tmp/monitor-export-XXXXXX";
    CHECK(mkdtemp(parallel_directory));
    HistoryStore parallel_store(parallel_directory, 64 * data.size(), 16 * data.size());
    CHECK(parallel_store.recover());
    for (unsigned i = 0; i < 48; ++i) {
        bucket.start_uptime_ms = i * 300000;
        bucket.sensors[0].average = 10.0 + i;
        bucket.sensors[0].minimum = 9.0 + i;
        bucket.sensors[0].maximum = 11.0 + i;
        CHECK(parallel_store.append(bucket));
    }
    std::array<std::string, 2> csv_exports;
    std::array<unsigned, 2> export_rows{};
    std::array<bool, 2> export_ok{};
    auto export_history = [&](size_t index) {
        csv_exports[index] = history_csv_header();
        export_ok[index] = parallel_store.visit([&](const Bucket &record) {
            for (const auto &reading : record.sensors) {
                csv_exports[index] += history_csv_row(record, reading, false);
                ++export_rows[index];
            }
            return true;
        });
    };
    std::thread first_export(export_history, 0);
    std::thread second_export(export_history, 1);
    first_export.join();
    second_export.join();
    CHECK(export_ok[0] && export_ok[1]);
    CHECK(export_rows[0] == 48 && export_rows[1] == 48);
    CHECK(csv_exports[0] == csv_exports[1]);
    std::filesystem::remove_all(parallel_directory);
    auto last = std::filesystem::path(directory) / "00000003.tmh";
    {
        std::ofstream corrupt(last, std::ios::binary | std::ios::app);
        // Simulate a real interrupted TMH2 append, not arbitrary junk.
        corrupt.write(reinterpret_cast<const char *>(data.data()), 7);
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
    // Even after manual repair, a failed recovery must leave this reused
    // writer disabled until it revalidates the entire history directory.
    CHECK(!store.append(bucket));
    CHECK(store.recover());
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

    // A symlink substituted for a recovered history segment must not be
    // followed for append, export, or recovery. The original data is preserved.
    char guarded_directory[] = "/tmp/monitor-segment-path-XXXXXX";
    CHECK(mkdtemp(guarded_directory));
    HistoryStore guarded(guarded_directory, 1024, 512);
    CHECK(guarded.recover());
    CHECK(guarded.append(bucket));
    const auto guarded_path = std::filesystem::path(guarded_directory) / "00000000.tmh";
    const auto preserved_path = std::filesystem::path(guarded_directory) / "preserved.bin";
    std::filesystem::rename(guarded_path, preserved_path);
    const auto preserved_bytes = std::filesystem::file_size(preserved_path);
    CHECK(symlink(preserved_path.c_str(), guarded_path.c_str()) == 0);
    CHECK(!guarded.append(bucket));
    CHECK(!guarded.visit([](const Bucket &) { return true; }));
    CHECK(!guarded.recover());
    CHECK(std::filesystem::file_size(preserved_path) == preserved_bytes);
    CHECK(std::filesystem::remove(guarded_path));
    CHECK(symlink("never-created.bin", guarded_path.c_str()) == 0);
    CHECK(!guarded.recover());
    const auto missing_segment = std::filesystem::path(guarded_directory) / "never-created.bin";
    CHECK(!std::filesystem::exists(missing_segment));
    CHECK(std::filesystem::remove(guarded_path));
    CHECK(std::filesystem::create_directory(guarded_path));
    CHECK(!guarded.recover());
    CHECK(std::filesystem::remove(guarded_path));
    CHECK(guarded.recover());
    CHECK(guarded.append(bucket));
    CHECK(guarded.visit([](const Bucket &) { return true; }));
    std::filesystem::remove_all(guarded_directory);

    // Linux /dev/full deterministically injects ENOSPC at the append boundary.
    char full_directory[] = "/tmp/monitor-full-XXXXXX";
    CHECK(mkdtemp(full_directory));
    const auto full_path = std::filesystem::path(full_directory) / "00000000.tmh";
    CHECK(symlink("/dev/full", full_path.c_str()) == 0);
    HistoryStore no_space(full_directory, 1024, 512);
    CHECK(no_space.recover());
    CHECK(!no_space.append(bucket));
    CHECK(no_space.bytes() == 0);
    std::filesystem::remove_all(full_directory);

    // An ENOSPC/flush failure may leave an incomplete record. Even if the
    // underlying path becomes writable again, a writer must not append to
    // potentially torn data until recovery has inspected the segment.
    char retry_directory[] = "/tmp/monitor-retry-XXXXXX";
    CHECK(mkdtemp(retry_directory));
    HistoryStore retry_store(retry_directory, 1024, 512);
    CHECK(retry_store.recover());
    CHECK(retry_store.append(bucket));
    const auto retry_path = std::filesystem::path(retry_directory) / "00000000.tmh";
    const auto retry_saved = std::filesystem::path(retry_directory) / "saved.tmh";
    std::filesystem::rename(retry_path, retry_saved);
    CHECK(symlink("/dev/full", retry_path.c_str()) == 0);
    bucket.start_uptime_ms += 300000;
    CHECK(!retry_store.append(bucket));
    CHECK(std::filesystem::remove(retry_path));
    std::filesystem::rename(retry_saved, retry_path);
    CHECK(!retry_store.append(bucket)); // Fail closed until successful recover().
    CHECK(retry_store.recover());
    CHECK(retry_store.append(bucket));
    unsigned retry_rows = 0;
    CHECK(retry_store.visit([&](const Bucket &) {
        ++retry_rows;
        return true;
    }));
    CHECK(retry_rows == 2);
    std::filesystem::remove_all(retry_directory);

    char event_directory[] = "/tmp/monitor-events-XXXXXX";
    CHECK(mkdtemp(event_directory));
    const std::string event_path = std::string(event_directory) + "/events.bin";
    JournalEvent event{};
    event.boot_id = 123;
    event.uptime_ms = 5000;
    event.kind = EventKind::sensor_state;
    event.previous = State::normal;
    event.state = State::high;
    event.rom = rom;
    event.utc_seconds = 1800000000;
    auto event_bytes = encode_event({1, event.boot_id, event.uptime_ms, event.utc_seconds,
                                     event.rom, event.kind, event.previous, event.state, 0});
    CHECK(event_bytes.size() == event_record_bytes);
    auto decoded_event = decode_event(event_bytes);
    CHECK(decoded_event && decoded_event->state == State::high &&
          decoded_event->utc_seconds == event.utc_seconds);
    for (auto kind : {EventKind::sensor_state, EventKind::wifi_state, EventKind::clock_sync,
                      EventKind::reboot, EventKind::storage_error}) {
        auto typed = event;
        typed.kind = kind;
        auto roundtrip =
            decode_event(encode_event({1, typed.boot_id, typed.uptime_ms, typed.utc_seconds,
                                       typed.rom, typed.kind, typed.previous, typed.state, 9}));
        CHECK(roundtrip && roundtrip->kind == kind && roundtrip->detail == 9);
    }
    for (size_t i = 0; i < event_bytes.size(); ++i) {
        auto damaged = event_bytes;
        damaged[i] ^= 1;
        CHECK(!decode_event(damaged));
    }
    // Recovery must never truncate or follow an existing unreadable,
    // redirected, or non-regular journal pathname. A dangling symlink was
    // previously treated as a missing file by ifstream and fopen("wb")
    // would create its target, making the symlink look legitimate.
    const std::string dangling_path = std::string(event_directory) + "/dangling.bin";
    const std::string dangling_target = std::string(event_directory) + "/never-create.bin";
    CHECK(symlink(dangling_target.c_str(), dangling_path.c_str()) == 0);
    EventStore dangling_journal(dangling_path, 4 * event_record_bytes);
    CHECK(!dangling_journal.recover());
    CHECK(std::filesystem::is_symlink(dangling_path));
    CHECK(!std::filesystem::exists(dangling_target));
    EventStore directory_journal(event_directory, 4 * event_record_bytes);
    CHECK(!directory_journal.recover());
    CHECK(std::filesystem::is_directory(event_directory));
    CHECK(std::filesystem::remove(dangling_path));
    // A link to an existing journal must not bypass the native path guard,
    // nor may recovery rewrite or truncate the original target.
    const std::string linked_target = std::string(event_directory) + "/existing.bin";
    const std::string linked_path = std::string(event_directory) + "/linked.bin";
    {
        std::ofstream original(linked_target, std::ios::binary);
        original.write(reinterpret_cast<const char *>(event_bytes.data()), event_bytes.size());
        CHECK(original.good());
    }
    CHECK(symlink(linked_target.c_str(), linked_path.c_str()) == 0);
    EventStore linked_journal(linked_path, 4 * event_record_bytes);
    CHECK(!linked_journal.recover());
    CHECK(std::filesystem::is_symlink(linked_path));
    CHECK(std::filesystem::file_size(linked_target) == event_bytes.size());
    {
        std::ifstream original(linked_target, std::ios::binary);
        std::vector<uint8_t> preserved((std::istreambuf_iterator<char>(original)), {});
        CHECK(preserved == event_bytes);
    }
    CHECK(std::filesystem::remove(linked_path));
    CHECK(std::filesystem::remove(linked_target));

    EventStore journal(event_path, 4 * event_record_bytes);
    CHECK(!journal.append(event)); // No implicit sequence reset on construction.
    CHECK(journal.bytes() == 0);
    CHECK(journal.recover());
    for (int64_t i = 0; i < 10; ++i) {
        event.uptime_ms = 5000 + i * 1000;
        event.state = (i % 2) ? State::normal : State::high;
        event.previous = (i % 2) ? State::high : State::normal;
        CHECK(journal.append(event));
    }
    // A fresh writer must not append sequence 1 to an existing journal
    // without recovering the durable last sequence and deduplication state.
    EventStore unchecked_events(event_path, 4 * event_record_bytes);
    const auto event_size = journal.bytes();
    CHECK(!unchecked_events.append(event));
    CHECK(unchecked_events.bytes() == event_size);
    const auto before_throttle = journal.bytes();
    event.uptime_ms += 500;
    CHECK(journal.append(event) && journal.bytes() == before_throttle);
    CHECK(journal.bytes() <= 4 * event_record_bytes);
    // A repeated event within one second may be coalesced, but a distinct
    // transition or newly known UTC timestamp must never disappear merely
    // because its final state and detail match the preceding event.
    const std::string dedup_path = std::string(event_directory) + "/dedup.bin";
    EventStore distinct_events(dedup_path, 8 * event_record_bytes);
    CHECK(distinct_events.recover());
    JournalEvent transition{};
    transition.boot_id = 987;
    transition.uptime_ms = 1000;
    transition.kind = EventKind::sensor_state;
    transition.rom = rom;
    transition.previous = State::normal;
    transition.state = State::high;
    CHECK(distinct_events.append(transition));
    transition.uptime_ms = 1500;
    CHECK(distinct_events.append(transition));
    CHECK(distinct_events.bytes() == event_record_bytes); // Exact duplicate.
    transition.previous = State::low;
    transition.uptime_ms = 1600;
    CHECK(distinct_events.append(transition));
    CHECK(distinct_events.bytes() == 2 * event_record_bytes);
    transition.utc_seconds = 1800000000;
    transition.uptime_ms = 1700;
    CHECK(distinct_events.append(transition));
    CHECK(distinct_events.bytes() == 3 * event_record_bytes);
    EventStore dedup_restarted(dedup_path, 8 * event_record_bytes);
    CHECK(dedup_restarted.recover());
    const auto distinct_page = dedup_restarted.page(0, 50);
    CHECK(distinct_page.available && distinct_page.events.size() == 3);
    CHECK(distinct_page.events[1].previous == State::low);
    CHECK(!distinct_page.events[1].utc_seconds);
    CHECK(distinct_page.events[2].utc_seconds == transition.utc_seconds);

    auto event_page = journal.page(0, 2);
    CHECK(event_page.events.size() == 2 && event_page.has_more && event_page.oldest_available > 1 &&
          event_page.next_after == event_page.events.back().sequence);
    EventStore reopened(event_path, 4 * event_record_bytes);
    CHECK(reopened.recover());
    const auto recovered_page = reopened.page(0, 50);
    const auto newest_sequence = recovered_page.next_after;
    event.boot_id = 456;
    event.kind = EventKind::reboot;
    event.rom = 0;
    event.uptime_ms = 0;
    event.utc_seconds.reset();
    event.previous = State::normal;
    event.state = State::normal;
    CHECK(reopened.append(event));
    auto after_reboot = reopened.page(newest_sequence, 50);
    CHECK(after_reboot.events.size() == 1 &&
          after_reboot.events.front().kind == EventKind::reboot &&
          after_reboot.events.front().sequence > newest_sequence);
    {
        std::ofstream tail(event_path, std::ios::binary | std::ios::app);
        // A genuine torn append starts with the current TME1 header.
        tail.write(reinterpret_cast<const char *>(event_bytes.data()), 7);
    }
    const auto valid_bytes = reopened.bytes() - 7;
    // A corrupted committed event plus a partial final append must not
    // truncate the tail before reporting the earlier corruption.
    {
        std::fstream corrupt(event_path, std::ios::binary | std::ios::in | std::ios::out);
        CHECK(corrupt.good());
        corrupt.put('X');
    }
    const auto damaged_with_tail = std::filesystem::file_size(event_path);
    EventStore invalid_before_tail(event_path, 4 * event_record_bytes);
    CHECK(!invalid_before_tail.recover());
    CHECK(std::filesystem::file_size(event_path) == damaged_with_tail);
    {
        std::fstream repair(event_path, std::ios::binary | std::ios::in | std::ios::out);
        CHECK(repair.good());
        repair.put('T');
    }
    EventStore tail_recovery(event_path, 4 * event_record_bytes);
    CHECK(tail_recovery.recover() && tail_recovery.bytes() == valid_bytes);
    {
        std::fstream corrupt(event_path, std::ios::binary | std::ios::in | std::ios::out);
        corrupt.put('X');
    }
    const auto damaged_size = std::filesystem::file_size(event_path);
    EventStore damaged_journal(event_path, 4 * event_record_bytes);
    CHECK(!damaged_journal.recover() && std::filesystem::file_size(event_path) == damaged_size);
    // A partial record advertising a newer journal format must not be
    // truncated by an older firmware image on restart.
    {
        std::ofstream future(event_path, std::ios::binary | std::ios::trunc);
        const char version_two[] = {'T', 'M', 'E', '2', 2, 0};
        future.write(version_two, sizeof(version_two));
        CHECK(future.good());
    }
    EventStore future_journal(event_path, 4 * event_record_bytes);
    CHECK(!future_journal.recover());
    CHECK(std::filesystem::file_size(event_path) == 6);
    // A reused store must discard its previous duplicate-suppression state
    // after an empty-file recovery or after recreating a missing journal.
    // Otherwise append() reports success but silently drops the first event.
    const std::string reused_path = std::string(event_directory) + "/reused.bin";
    EventStore reused(reused_path, 4 * event_record_bytes);
    CHECK(reused.recover());
    CHECK(reused.append(event));
    CHECK(reused.bytes() == event_record_bytes);
    {
        std::ofstream empty(reused_path, std::ios::binary | std::ios::trunc);
        CHECK(empty.good());
    }
    CHECK(reused.recover());
    CHECK(reused.append(event));
    CHECK(reused.bytes() == event_record_bytes);
    CHECK(std::filesystem::remove(reused_path));
    CHECK(reused.recover());
    CHECK(reused.append(event));
    CHECK(reused.bytes() == event_record_bytes);
    // A failed recovery must not partially advance in-memory sequence state.
    auto later = event;
    later.uptime_ms += 1000;
    CHECK(reused.append(later));
    {
        std::fstream corrupt(reused_path, std::ios::binary | std::ios::in | std::ios::out);
        CHECK(corrupt.good());
        corrupt.seekp(static_cast<std::streamoff>(event_record_bytes));
        corrupt.put('X');
    }
    CHECK(!reused.recover());
    CHECK(std::filesystem::file_size(reused_path) == 2 * event_record_bytes);
    {
        std::fstream repair(reused_path, std::ios::binary | std::ios::in | std::ios::out);
        CHECK(repair.good());
        repair.seekp(static_cast<std::streamoff>(event_record_bytes));
        repair.put('T');
    }
    CHECK(!reused.append(later)); // Corruption was repaired, not revalidated.
    CHECK(reused.recover());
    later.uptime_ms += 1000;
    CHECK(reused.append(later));
    CHECK(reused.page(0, 50).events.back().sequence == 3);

    // A failed event append must not be retried against a possibly torn
    // journal, even if the filesystem becomes writable again. Recovery
    // restores sequence continuity before allowing another append.
    const std::string retry_event_path = std::string(event_directory) + "/retry.bin";
    EventStore retry_events(retry_event_path, 4 * event_record_bytes);
    CHECK(retry_events.recover());
    CHECK(retry_events.append(event));
    const auto retry_event_saved = retry_event_path + ".saved";
    std::filesystem::rename(retry_event_path, retry_event_saved);
    CHECK(symlink("/dev/full", retry_event_path.c_str()) == 0);
    auto next_event = event;
    next_event.uptime_ms += 2000;
    CHECK(!retry_events.append(next_event));
    CHECK(std::filesystem::remove(retry_event_path));
    std::filesystem::rename(retry_event_saved, retry_event_path);
    CHECK(!retry_events.append(next_event));
    CHECK(retry_events.recover());
    CHECK(retry_events.append(next_event));
    const auto retry_page = retry_events.page(0, 50);
    CHECK(retry_page.available && retry_page.events.size() == 2);
    CHECK(retry_page.events[0].sequence == 1 && retry_page.events[1].sequence == 2);

    // A valid-CRC journal that exceeds its configured capacity is not a
    // recoverable retention rollover. Reject reads and refuse compaction,
    // preserving all bytes for diagnosis rather than silently deleting them.
    const std::string oversized_path = std::string(event_directory) + "/oversized.bin";
    EventStore oversized(oversized_path, 2 * event_record_bytes);
    CHECK(oversized.recover());
    {
        std::ofstream out(oversized_path, std::ios::binary | std::ios::trunc);
        for (const auto sequence : {uint64_t(1), uint64_t(2), uint64_t(3)}) {
            auto serialized = event;
            serialized.sequence = sequence;
            const auto encoded = encode_event(serialized);
            CHECK(encoded.size() == event_record_bytes);
            out.write(reinterpret_cast<const char *>(encoded.data()), encoded.size());
        }
        CHECK(out.good());
    }
    CHECK(oversized.bytes() == 3 * event_record_bytes);
    CHECK(!oversized.page(0, 1).available);
    auto oversized_next = event;
    oversized_next.uptime_ms += 2000;
    CHECK(!oversized.append(oversized_next));
    CHECK(oversized.bytes() == 3 * event_record_bytes);
    CHECK(!oversized.recover());
    CHECK(std::filesystem::file_size(oversized_path) == 3 * event_record_bytes);
    std::filesystem::resize_file(oversized_path, 2 * event_record_bytes);
    CHECK(oversized.recover());
    CHECK(oversized.page(0, 50).events.size() == 2);
    CHECK(oversized.append(oversized_next));
    CHECK(oversized.page(0, 50).events.back().sequence == 3);

    // CRC-valid but non-monotonic sequences must not be served to API
    // clients or compacted: recovery already rejects these journals.
    const std::string sequence_path = std::string(event_directory) + "/sequence.bin";
    for (const auto &pair : {std::array<uint64_t, 2>{2, 1}, std::array<uint64_t, 2>{1, 1}}) {
        std::ofstream out(sequence_path, std::ios::binary | std::ios::trunc);
        for (const auto sequence : pair) {
            auto serialized = event;
            serialized.sequence = sequence;
            const auto encoded = encode_event(serialized);
            CHECK(encoded.size() == event_record_bytes);
            out.write(reinterpret_cast<const char *>(encoded.data()), encoded.size());
        }
        out.close();
        CHECK(out.good());
        EventStore unordered(sequence_path, 2 * event_record_bytes);
        CHECK(!unordered.recover());
        CHECK(!unordered.page(0, 50).available);
        CHECK(!unordered.page(0, 1).available);
    }
    {
        std::ofstream out(sequence_path, std::ios::binary | std::ios::trunc);
        for (const auto sequence : {uint64_t(1), uint64_t(3)}) {
            auto serialized = event;
            serialized.sequence = sequence;
            const auto encoded = encode_event(serialized);
            out.write(reinterpret_cast<const char *>(encoded.data()), encoded.size());
        }
        out.close();
        CHECK(out.good());
    }
    EventStore gapped(sequence_path, 2 * event_record_bytes);
    CHECK(gapped.recover()); // Compaction may legitimately leave sequence gaps.
    CHECK(gapped.page(0, 50).events.size() == 2);
    {
        std::fstream tamper(sequence_path, std::ios::binary | std::ios::in | std::ios::out);
        auto serialized = event;
        serialized.sequence = 1;
        const auto encoded = encode_event(serialized);
        tamper.seekp(event_record_bytes);
        tamper.write(reinterpret_cast<const char *>(encoded.data()), encoded.size());
        CHECK(tamper.good());
    }
    auto next_sequence_event = event;
    next_sequence_event.uptime_ms += 10000;
    CHECK(!gapped.append(next_sequence_event)); // Compact refuses reordered records.
    {
        std::fstream repair(sequence_path, std::ios::binary | std::ios::in | std::ios::out);
        auto restored = event;
        restored.sequence = 3;
        const auto encoded = encode_event(restored);
        repair.seekp(event_record_bytes);
        repair.write(reinterpret_cast<const char *>(encoded.data()), encoded.size());
        CHECK(repair.good());
    }
    CHECK(!gapped.append(next_sequence_event)); // Failed compaction latches writer.
    CHECK(gapped.recover());
    CHECK(gapped.append(next_sequence_event));
    CHECK(gapped.page(0, 50).events.back().sequence == 4);
    // Even a one-event API page must fail closed when a later record is
    // CRC-valid but out of sequence, or when the final append is torn.
    const std::string late_path = std::string(event_directory) + "/late.bin";
    {
        std::ofstream out(late_path, std::ios::binary | std::ios::trunc);
        for (const auto sequence : {uint64_t(1), uint64_t(2), uint64_t(3)}) {
            auto serialized = event;
            serialized.sequence = sequence;
            const auto encoded = encode_event(serialized);
            CHECK(encoded.size() == event_record_bytes);
            out.write(reinterpret_cast<const char *>(encoded.data()), encoded.size());
        }
        CHECK(out.good());
    }
    EventStore late_journal(late_path, 4 * event_record_bytes);
    CHECK(late_journal.recover());
    const auto first_page = late_journal.page(0, 1);
    CHECK(first_page.available && first_page.events.size() == 1 && first_page.has_more &&
          first_page.next_after == 1);
    {
        std::fstream out(late_path, std::ios::binary | std::ios::in | std::ios::out);
        auto serialized = event;
        serialized.sequence = 1; // Duplicate after the requested page.
        const auto encoded = encode_event(serialized);
        out.seekp(2 * event_record_bytes);
        out.write(reinterpret_cast<const char *>(encoded.data()), encoded.size());
        CHECK(out.good());
    }
    CHECK(!late_journal.page(0, 1).available);
    CHECK(!late_journal.page(0, 2).available);
    {
        std::fstream out(late_path, std::ios::binary | std::ios::in | std::ios::out);
        auto serialized = event;
        serialized.sequence = 3;
        const auto encoded = encode_event(serialized);
        out.seekp(2 * event_record_bytes);
        out.write(reinterpret_cast<const char *>(encoded.data()), encoded.size());
        CHECK(out.good());
    }
    CHECK(late_journal.page(0, 1).available);
    {
        std::ofstream out(late_path, std::ios::binary | std::ios::app);
        out.write("cut", 3);
        CHECK(out.good());
    }
    CHECK(!late_journal.page(0, 1).available);
    std::filesystem::remove_all(event_directory);
    char cadence_directory[] = "/tmp/monitor-cadence-XXXXXX";
    CHECK(mkdtemp(cadence_directory));
    HistoryStore cadence_store(cadence_directory, 32 * 1024, 8 * 1024);
    CHECK(cadence_store.recover());
    Registry registry;
    Aggregator aggregation(123);
    unsigned windows = 0;
    // Accelerated 24-hour run. Only completed 5-minute buckets reach persistent storage.
    for (int64_t now = 0; now <= 86400000; now += sample_ms) {
        registry.sample({{rom, 20.0}}, now);
        auto result = aggregation.add(registry, now);
        if (result) {
            ++windows;
            CHECK(result->sensors[0].count == 60);
            CHECK(!result->sensors[0].partial);
            CHECK(cadence_store.append(*result));
        }
    }
    CHECK(windows == 288);
    const auto expected_cadence_bytes =
        windows *
        encode_bucket(
            Bucket{123, 0, std::nullopt, {{rom, 20.0, 20.0, 20.0, 60, false, "Sensor 1", 0}}})
            .size();
    CHECK(cadence_store.bytes() == expected_cadence_bytes);
    unsigned persisted_windows = 0;
    CHECK(cadence_store.visit([&](const Bucket &record) {
        CHECK(record.sensors.size() == 1 && record.sensors[0].count == 60);
        ++persisted_windows;
        return true;
    }));
    CHECK(persisted_windows == windows);
    HistoryStore cadence_reopened(cadence_directory, 32 * 1024, 8 * 1024);
    CHECK(cadence_reopened.recover());
    CHECK(cadence_reopened.bytes() == expected_cadence_bytes);
    std::filesystem::remove_all(cadence_directory);
    std::cout << "PASS: binary CRC, truncation, rotation, retention, recovery, persisted 5-minute "
                 "cadence and 24-hour aggregation\n";
}
