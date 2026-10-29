#include "event_store.hpp"
#include "fake_bus.hpp"
#include "history.hpp"
#include "history_csv.hpp"
#include "history_mount_policy.hpp"
#include <algorithm>
#include <array>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sys/resource.h>
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
    // Inject a deterministic buffered-write/flush failure on a regular file.
    // Symlinked /dev/full paths are now intentionally rejected by HistoryStore.
    const auto with_file_limit = [](rlim_t bytes, const std::function<void()> &action) {
        struct rlimit original {};
        CHECK(getrlimit(RLIMIT_FSIZE, &original) == 0);
        struct rlimit constrained = original;
        constrained.rlim_cur = bytes;
        auto previous = std::signal(SIGXFSZ, SIG_IGN);
        CHECK(previous != SIG_ERR);
        CHECK(setrlimit(RLIMIT_FSIZE, &constrained) == 0);
        action();
        CHECK(setrlimit(RLIMIT_FSIZE, &original) == 0);
        CHECK(std::signal(SIGXFSZ, previous) != SIG_ERR);
    };
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
    // A same-name ordinary replacement file must not receive an append just
    // because it is regular and contains an otherwise valid history record.
    const auto guarded_original = std::filesystem::path(guarded_directory) / "original.tmh";
    std::filesystem::rename(guarded_path, guarded_original);
    CHECK(std::filesystem::copy_file(guarded_original, guarded_path));
    const auto replacement_bytes = std::filesystem::file_size(guarded_path);
    CHECK(!guarded.append(bucket));
    CHECK(std::filesystem::file_size(guarded_path) == replacement_bytes);
    CHECK(!guarded.append(bucket)); // Refusal latches writes until recovery.
    CHECK(std::filesystem::remove(guarded_path));
    std::filesystem::rename(guarded_original, guarded_path);
    CHECK(guarded.recover());
    CHECK(guarded.append(bucket));
    unsigned restored_segment_rows = 0;
    CHECK(guarded.visit([&](const Bucket &) {
        ++restored_segment_rows;
        return true;
    }));
    CHECK(restored_segment_rows == 2);
    std::filesystem::remove_all(guarded_directory);

    // Same-inode in-place truncation or external growth must not silently
    // erase samples or permit appending to an unvalidated active segment.
    char size_directory[] = "/tmp/monitor-segment-size-XXXXXX";
    CHECK(mkdtemp(size_directory));
    HistoryStore size_guard(size_directory, 8 * data.size(), 4 * data.size());
    CHECK(size_guard.recover());
    CHECK(size_guard.append(bucket));
    const auto size_path = std::filesystem::path(size_directory) / "00000000.tmh";
    CHECK(std::filesystem::file_size(size_path) == data.size());
    std::filesystem::resize_file(size_path, 0);
    CHECK(!size_guard.append(bucket));
    CHECK(!size_guard.append(bucket)); // A size mismatch latches the writer.
    CHECK(size_guard.recover());
    CHECK(size_guard.append(bucket));
    {
        std::ofstream out(size_path, std::ios::binary | std::ios::app);
        out.write(reinterpret_cast<const char *>(data.data()), data.size());
        CHECK(out.good());
    }
    CHECK(!size_guard.append(bucket));
    CHECK(!size_guard.append(bucket));
    CHECK(size_guard.recover());
    CHECK(size_guard.append(bucket));
    unsigned size_rows = 0;
    CHECK(size_guard.visit([&](const Bucket &) {
        ++size_rows;
        return true;
    }));
    CHECK(size_rows == 3);
    std::filesystem::remove_all(size_directory);

    // Rollover must never follow a pre-created next-segment symlink, and
    // deletion of the active segment after recovery must not be masked.
    char rollover_directory[] = "/tmp/monitor-rollover-path-XXXXXX";
    CHECK(mkdtemp(rollover_directory));
    HistoryStore rollover_guard(rollover_directory, 4 * data.size(), data.size());
    CHECK(rollover_guard.recover());
    CHECK(rollover_guard.append(bucket));
    const auto rollover_path = std::filesystem::path(rollover_directory) / "00000001.tmh";
    const auto rollover_target = std::filesystem::path(rollover_directory) / "sentinel.bin";
    {
        std::ofstream sentinel(rollover_target, std::ios::binary);
        sentinel << "untouched";
    }
    CHECK(symlink(rollover_target.c_str(), rollover_path.c_str()) == 0);
    CHECK(!rollover_guard.append(bucket));
    CHECK(std::filesystem::file_size(rollover_target) == 9);
    CHECK(!rollover_guard.append(bucket)); // Invalid path latches writer.
    CHECK(std::filesystem::remove(rollover_path));
    CHECK(rollover_guard.recover());
    CHECK(rollover_guard.append(bucket));
    unsigned rollover_rows = 0;
    CHECK(rollover_guard.visit([&](const Bucket &) {
        ++rollover_rows;
        return true;
    }));
    CHECK(rollover_rows == 2);
    CHECK(std::filesystem::file_size(rollover_target) == 9);
    const auto current_rollover = std::filesystem::path(rollover_directory) / "00000001.tmh";
    CHECK(std::filesystem::remove(current_rollover));
    CHECK(!rollover_guard.append(bucket)); // Missing active file is not recreated.
    CHECK(!std::filesystem::exists(current_rollover));
    std::filesystem::remove_all(rollover_directory);

    // The history directory itself may be replaced after recovery. A host
    // symlink to the original directory must not redirect appends/exports.
    char directory_guard_path[] = "/tmp/monitor-directory-XXXXXX";
    CHECK(mkdtemp(directory_guard_path));
    HistoryStore directory_guard(directory_guard_path, 4 * data.size(), 2 * data.size());
    CHECK(directory_guard.recover());
    CHECK(directory_guard.append(bucket));
    const std::filesystem::path moved_directory(std::string(directory_guard_path) + "-saved");
    std::filesystem::rename(directory_guard_path, moved_directory);
    CHECK(symlink(moved_directory.c_str(), directory_guard_path) == 0);
    CHECK(!directory_guard.append(bucket));
    CHECK(!directory_guard.visit([](const Bucket &) { return true; }));
    CHECK(!directory_guard.recover());
    CHECK(std::filesystem::file_size(moved_directory / "00000000.tmh") == data.size());
    CHECK(std::filesystem::remove(directory_guard_path));
    std::filesystem::rename(moved_directory, directory_guard_path);
    CHECK(directory_guard.recover());
    CHECK(directory_guard.append(bucket));
    // A different ordinary history directory with the same valid contents
    // is not the recovered writer's directory and must not receive appends.
    const auto original_segment = std::filesystem::path(directory_guard_path) / "00000000.tmh";
    const auto original_length = std::filesystem::file_size(original_segment);
    std::filesystem::rename(directory_guard_path, moved_directory);
    CHECK(std::filesystem::create_directory(directory_guard_path));
    const auto substitute_segment = std::filesystem::path(directory_guard_path) / "00000000.tmh";
    CHECK(std::filesystem::copy_file(moved_directory / "00000000.tmh", substitute_segment));
    CHECK(!directory_guard.append(bucket));
    CHECK(std::filesystem::file_size(substitute_segment) == original_length);
    CHECK(!directory_guard.append(bucket)); // Failed identity check latches writer.
    std::filesystem::remove_all(directory_guard_path);
    std::filesystem::rename(moved_directory, directory_guard_path);
    CHECK(directory_guard.recover());
    CHECK(directory_guard.append(bucket));
    std::filesystem::remove_all(directory_guard_path);
    CHECK(!directory_guard.visit([](const Bucket &) { return true; }));
    CHECK(!directory_guard.append(bucket));

    // A history export callback can run long enough for its parent
    // directory to be replaced between segments. Do not follow a substituted
    // directory for the next segment or report a successful export.
    char visit_directory[] = "/tmp/monitor-visit-directory-XXXXXX";
    CHECK(mkdtemp(visit_directory));
    HistoryStore visit_guard(visit_directory, 4 * data.size(), data.size());
    CHECK(visit_guard.recover());
    bucket.start_uptime_ms = 0;
    CHECK(visit_guard.append(bucket));
    bucket.start_uptime_ms = 300000;
    CHECK(visit_guard.append(bucket));
    const auto visit_saved = std::filesystem::path(std::string(visit_directory) + "-saved");
    unsigned interrupted_rows = 0;
    CHECK(!visit_guard.visit([&](const Bucket &) {
        ++interrupted_rows;
        if (interrupted_rows == 1) {
            std::filesystem::rename(visit_directory, visit_saved);
            CHECK(symlink(visit_saved.c_str(), visit_directory) == 0);
        }
        return true;
    }));
    CHECK(interrupted_rows == 1);
    CHECK(std::filesystem::file_size(visit_saved / "00000000.tmh") == data.size());
    CHECK(std::filesystem::file_size(visit_saved / "00000001.tmh") == data.size());
    CHECK(std::filesystem::remove(visit_directory));
    std::filesystem::rename(visit_saved, visit_directory);
    unsigned restored_rows = 0;
    CHECK(visit_guard.visit([&](const Bucket &) {
        ++restored_rows;
        return true;
    }));
    CHECK(restored_rows == 2);
    std::filesystem::remove_all(visit_directory);

    // A second ordinary directory with valid records must not be allowed
    // to splice a history export across different directories.
    char regular_visit_directory[] = "/tmp/monitor-visit-regular-XXXXXX";
    CHECK(mkdtemp(regular_visit_directory));
    HistoryStore regular_visit_guard(regular_visit_directory, 4 * data.size(), data.size());
    CHECK(regular_visit_guard.recover());
    bucket.start_uptime_ms = 0;
    CHECK(regular_visit_guard.append(bucket));
    bucket.start_uptime_ms = 300000;
    CHECK(regular_visit_guard.append(bucket));
    const auto regular_saved =
        std::filesystem::path(std::string(regular_visit_directory) + "-saved");
    unsigned substituted_rows = 0;
    CHECK(!regular_visit_guard.visit([&](const Bucket &) {
        ++substituted_rows;
        if (substituted_rows == 1) {
            std::filesystem::rename(regular_visit_directory, regular_saved);
            CHECK(std::filesystem::create_directory(regular_visit_directory));
            for (unsigned i = 0; i < 2; ++i) {
                const auto filename = i ? "00000001.tmh" : "00000000.tmh";
                CHECK(std::filesystem::copy_file(regular_saved / filename,
                                                 std::filesystem::path(regular_visit_directory) /
                                                     filename));
            }
        }
        return true;
    }));
    CHECK(substituted_rows == 1);
    CHECK(std::filesystem::file_size(regular_saved / "00000000.tmh") == data.size());
    CHECK(std::filesystem::file_size(regular_saved / "00000001.tmh") == data.size());
    std::filesystem::remove_all(regular_visit_directory);
    std::filesystem::rename(regular_saved, regular_visit_directory);
    unsigned regular_restored = 0;
    CHECK(regular_visit_guard.visit([&](const Bucket &) {
        ++regular_restored;
        return true;
    }));
    CHECK(regular_restored == 2);
    std::filesystem::remove_all(regular_visit_directory);

    // A same-name, same-size ordinary segment replacement during a callback
    // must not splice data from an unvalidated file into a CSV export.
    char segment_visit_directory[] = "/tmp/monitor-visit-segment-XXXXXX";
    CHECK(mkdtemp(segment_visit_directory));
    HistoryStore segment_visit_guard(segment_visit_directory, 8 * data.size(), 3 * data.size());
    CHECK(segment_visit_guard.recover());
    bucket.start_uptime_ms = 0;
    CHECK(segment_visit_guard.append(bucket));
    bucket.start_uptime_ms = 300000;
    CHECK(segment_visit_guard.append(bucket));
    const auto segment_visit_path = std::filesystem::path(segment_visit_directory) / "00000000.tmh";
    const auto segment_visit_saved =
        std::filesystem::path(segment_visit_directory) / "original.bin";
    unsigned segment_visit_rows = 0;
    CHECK(!segment_visit_guard.visit([&](const Bucket &) {
        ++segment_visit_rows;
        if (segment_visit_rows == 1) {
            std::filesystem::rename(segment_visit_path, segment_visit_saved);
            CHECK(std::filesystem::copy_file(segment_visit_saved, segment_visit_path));
            CHECK(std::filesystem::file_size(segment_visit_path) == 2 * data.size());
        }
        return true;
    }));
    CHECK(segment_visit_rows == 1);
    CHECK(std::filesystem::remove(segment_visit_path));
    std::filesystem::rename(segment_visit_saved, segment_visit_path);
    unsigned segment_restored_rows = 0;
    CHECK(segment_visit_guard.visit([&](const Bucket &) {
        ++segment_restored_rows;
        return true;
    }));
    CHECK(segment_restored_rows == 2);
    std::filesystem::remove_all(segment_visit_directory);

    // Retention must not erase older, valid segments before a new record is
    // durable. Force a write failure precisely when rollover would evict the
    // oldest segment; all four original records must remain readable.
    char eviction_directory[] = "/tmp/monitor-eviction-XXXXXX";
    CHECK(mkdtemp(eviction_directory));
    HistoryStore eviction_store(eviction_directory, 4 * data.size(), 2 * data.size());
    CHECK(eviction_store.recover());
    for (unsigned i = 0; i < 4; ++i) {
        bucket.start_uptime_ms = i * 300000;
        CHECK(eviction_store.append(bucket));
    }
    const auto oldest_segment = std::filesystem::path(eviction_directory) / "00000000.tmh";
    CHECK(std::filesystem::file_size(oldest_segment) == 2 * data.size());
    bucket.start_uptime_ms = 4 * 300000;
    with_file_limit(0, [&] { CHECK(!eviction_store.append(bucket)); });
    CHECK(std::filesystem::file_size(oldest_segment) == 2 * data.size());
    unsigned retained_count = 0;
    int64_t retained_first = -1;
    CHECK(eviction_store.visit([&](const Bucket &record) {
        if (retained_first < 0)
            retained_first = record.start_uptime_ms;
        ++retained_count;
        return true;
    }));
    CHECK(retained_count == 4 && retained_first == 0);
    CHECK(!eviction_store.append(bucket)); // A failed write latches the writer.
    CHECK(eviction_store.recover());
    CHECK(eviction_store.append(bucket));
    CHECK(eviction_store.bytes() <= 4 * data.size());
    retained_count = 0;
    retained_first = -1;
    CHECK(eviction_store.visit([&](const Bucket &record) {
        if (retained_first < 0)
            retained_first = record.start_uptime_ms;
        ++retained_count;
        return true;
    }));
    CHECK(retained_count == 3 && retained_first == 2 * 300000);
    std::filesystem::remove_all(eviction_directory);

    // A zero-byte RLIMIT_FSIZE causes a real buffered-write/flush failure
    // without redirecting the history path through a prohibited symlink.
    char full_directory[] = "/tmp/monitor-full-XXXXXX";
    CHECK(mkdtemp(full_directory));
    HistoryStore no_space(full_directory, 1024, 512);
    CHECK(no_space.recover());
    with_file_limit(0, [&] { CHECK(!no_space.append(bucket)); });
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
    const auto retry_bytes = std::filesystem::file_size(retry_path);
    bucket.start_uptime_ms += 300000;
    with_file_limit(retry_bytes, [&] { CHECK(!retry_store.append(bucket)); });
    CHECK(std::filesystem::file_size(retry_path) == retry_bytes);
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

    // Replacing a validated journal with a symlink after recovery must not
    // redirect append writes or expose the substituted target via the API.
    const std::string swapped_path = std::string(event_directory) + "/swapped.bin";
    const std::string swapped_saved = swapped_path + ".saved";
    EventStore swapped(swapped_path, 4 * event_record_bytes);
    CHECK(swapped.recover());
    CHECK(swapped.append(event));
    std::filesystem::rename(swapped_path, swapped_saved);
    CHECK(symlink(swapped_saved.c_str(), swapped_path.c_str()) == 0);
    auto swapped_next = event;
    swapped_next.uptime_ms += 2000;
    CHECK(!swapped.append(swapped_next));
    CHECK(!swapped.page(0, 1).available);
    CHECK(std::filesystem::file_size(swapped_saved) == event_record_bytes);
    CHECK(std::filesystem::remove(swapped_path));
    std::filesystem::rename(swapped_saved, swapped_path);
    CHECK(!swapped.append(swapped_next)); // Failed write requires revalidation.
    CHECK(swapped.recover());
    CHECK(swapped.append(swapped_next));
    CHECK(swapped.page(0, 50).events.size() == 2);

    // Compaction must not truncate a pre-existing temporary path. A symlink
    // to an unrelated file is preserved, and the journal stays recoverable.
    const std::string compact_path = std::string(event_directory) + "/compact.bin";
    const std::string compact_temp = compact_path + ".tmp";
    const std::string compact_target = compact_path + ".preserved";
    EventStore compact_journal(compact_path, 2 * event_record_bytes);
    CHECK(compact_journal.recover());
    CHECK(compact_journal.append(event));
    auto compact_second = event;
    compact_second.uptime_ms += 2000;
    CHECK(compact_journal.append(compact_second));
    {
        std::ofstream out(compact_target, std::ios::binary);
        out << "preserve-this-file";
        CHECK(out.good());
    }
    CHECK(symlink(compact_target.c_str(), compact_temp.c_str()) == 0);
    auto compact_third = compact_second;
    compact_third.uptime_ms += 2000;
    CHECK(!compact_journal.append(compact_third));
    CHECK(std::filesystem::file_size(compact_path) == 2 * event_record_bytes);
    {
        std::ifstream in(compact_target, std::ios::binary);
        std::string preserved((std::istreambuf_iterator<char>(in)), {});
        CHECK(preserved == "preserve-this-file");
    }
    CHECK(std::filesystem::remove(compact_temp));
    CHECK(!compact_journal.append(compact_third)); // Failure latches writer.
    CHECK(compact_journal.recover());
    CHECK(compact_journal.append(compact_third));
    const auto compact_page = compact_journal.page(0, 50);
    CHECK(compact_page.available && compact_page.events.size() == 2);
    CHECK(compact_page.events[0].sequence == 2 && compact_page.events[1].sequence == 3);

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
    CHECK(!retry_events.page(0, 50).available); // Unvalidated journal is not API-healthy.
    CHECK(retry_events.recover());
    CHECK(retry_events.page(0, 50).available);
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
    // The original valid bytes and inode are restored, but the failed writer
    // must not expose them as verified until explicit recovery succeeds.
    CHECK(!retry_events.page(0, 50).available);
    CHECK(retry_events.recover());
    CHECK(retry_events.page(0, 50).available);
    CHECK(retry_events.append(next_event));
    const auto retry_page = retry_events.page(0, 50);
    CHECK(retry_page.available && retry_page.events.size() == 2);
    CHECK(retry_page.events[0].sequence == 1 && retry_page.events[1].sequence == 2);

    // An ordinary replacement journal with valid bytes is not the recovered
    // file. Reject append and API pagination, preserve the substitute, and
    // permit writes only after the original is restored and revalidated.
    const std::string replaced_event_path = std::string(event_directory) + "/replaced.bin";
    EventStore replaced_events(replaced_event_path, 4 * event_record_bytes);
    CHECK(replaced_events.recover());
    CHECK(replaced_events.append(event));
    const auto saved_event_path = replaced_event_path + ".saved";
    std::filesystem::rename(replaced_event_path, saved_event_path);
    CHECK(std::filesystem::copy_file(saved_event_path, replaced_event_path));
    const auto replacement_event_bytes = std::filesystem::file_size(replaced_event_path);
    CHECK(!replaced_events.page(0, 50).available);
    CHECK(!replaced_events.append(next_event));
    CHECK(std::filesystem::file_size(replaced_event_path) == replacement_event_bytes);
    CHECK(!replaced_events.append(next_event));
    CHECK(std::filesystem::remove(replaced_event_path));
    std::filesystem::rename(saved_event_path, replaced_event_path);
    CHECK(replaced_events.recover());
    CHECK(replaced_events.append(next_event));
    CHECK(replaced_events.page(0, 50).events.size() == 2);

    // A same-inode truncate or external append must not silently reset
    // sequences or be hidden by duplicate suppression. A fresh recovery is
    // required before either the writer or API page may trust the journal.
    const std::string changed_size_path = std::string(event_directory) + "/changed-size.bin";
    EventStore event_size_guard(changed_size_path, 4 * event_record_bytes);
    CHECK(event_size_guard.recover());
    auto size_first = event;
    size_first.uptime_ms = 1000;
    CHECK(event_size_guard.append(size_first));
    CHECK(event_size_guard.bytes() == event_record_bytes);
    std::filesystem::resize_file(changed_size_path, 0);
    CHECK(!event_size_guard.append(size_first)); // Would previously deduplicate stale state.
    CHECK(!event_size_guard.page(0, 50).available);
    CHECK(!event_size_guard.append(next_event)); // Failure latches the writer.
    CHECK(event_size_guard.recover());
    CHECK(event_size_guard.append(size_first));
    CHECK(event_size_guard.page(0, 50).events.size() == 1);
    auto externally_added = size_first;
    externally_added.sequence = 2;
    externally_added.uptime_ms += 2000;
    {
        const auto encoded = encode_event(externally_added);
        CHECK(encoded.size() == event_record_bytes);
        std::ofstream out(changed_size_path, std::ios::binary | std::ios::app);
        out.write(reinterpret_cast<const char *>(encoded.data()), encoded.size());
        CHECK(out.good());
    }
    CHECK(!event_size_guard.append(next_event));
    CHECK(!event_size_guard.page(0, 50).available);
    CHECK(event_size_guard.recover());
    auto after_external = externally_added;
    after_external.uptime_ms += 2000;
    CHECK(event_size_guard.append(after_external));
    const auto restored_size_page = event_size_guard.page(0, 50);
    CHECK(restored_size_page.available && restored_size_page.events.size() == 3 &&
          restored_size_page.events.back().sequence == 3);

    // Same-size, same-inode corruption must not bypass duplicate suppression.
    const std::string altered_last_path = std::string(event_directory) + "/altered-last.bin";
    EventStore altered_last(altered_last_path, 4 * event_record_bytes);
    CHECK(altered_last.recover());
    CHECK(altered_last.append(event));
    auto first_durable = event;
    first_durable.sequence = 1;
    const auto first_bytes = encode_event(first_durable);
    CHECK(first_bytes.size() == event_record_bytes);
    {
        std::fstream damaged(altered_last_path, std::ios::binary | std::ios::in | std::ios::out);
        CHECK(damaged.good());
        damaged.seekp(12);
        damaged.put(static_cast<char>(first_bytes[12] ^ 1));
        CHECK(damaged.good());
    }
    CHECK(!altered_last.append(event)); // Must not acknowledge a stale duplicate.
    CHECK(!altered_last.page(0, 50).available);
    CHECK(!altered_last.append(next_event)); // Failed verification latches writes.
    CHECK(std::filesystem::file_size(altered_last_path) == event_record_bytes);
    {
        std::fstream repaired(altered_last_path, std::ios::binary | std::ios::in | std::ios::out);
        CHECK(repaired.good());
        repaired.seekp(12);
        repaired.put(static_cast<char>(first_bytes[12]));
        CHECK(repaired.good());
    }
    CHECK(altered_last.recover());
    CHECK(altered_last.append(next_event));
    auto second_durable = next_event;
    second_durable.sequence = 2;
    const auto second_bytes = encode_event(second_durable);
    CHECK(second_bytes.size() == event_record_bytes);
    // A same-length, CRC-valid change to the final record must not be
    // served by an API page, including a page that ends before that record.
    auto valid_last_change = second_durable;
    valid_last_change.kind = EventKind::storage_error;
    const auto changed_last_bytes = encode_event(valid_last_change);
    CHECK(changed_last_bytes.size() == event_record_bytes);
    {
        std::fstream changed(altered_last_path, std::ios::binary | std::ios::in | std::ios::out);
        CHECK(changed.good());
        changed.seekp(static_cast<std::streamoff>(event_record_bytes));
        changed.write(reinterpret_cast<const char *>(changed_last_bytes.data()),
                      changed_last_bytes.size());
        CHECK(changed.good());
    }
    CHECK(std::filesystem::file_size(altered_last_path) == 2 * event_record_bytes);
    CHECK(!altered_last.page(0, 1).available);
    CHECK(!altered_last.page(0, 50).available);
    {
        std::fstream restored(altered_last_path, std::ios::binary | std::ios::in | std::ios::out);
        CHECK(restored.good());
        restored.seekp(static_cast<std::streamoff>(event_record_bytes));
        restored.write(reinterpret_cast<const char *>(second_bytes.data()), second_bytes.size());
        CHECK(restored.good());
    }
    CHECK(altered_last.page(0, 1).available);
    {
        std::fstream damaged(altered_last_path, std::ios::binary | std::ios::in | std::ios::out);
        CHECK(damaged.good());
        damaged.seekp(static_cast<std::streamoff>(event_record_bytes + 48));
        damaged.put(static_cast<char>(second_bytes[48] ^ 1));
        CHECK(damaged.good());
    }
    auto third_event = next_event;
    third_event.uptime_ms += 2000;
    CHECK(!altered_last.append(third_event)); // Distinct writes also fail closed.
    CHECK(!altered_last.page(0, 50).available);
    {
        std::fstream repaired(altered_last_path, std::ios::binary | std::ios::in | std::ios::out);
        CHECK(repaired.good());
        repaired.seekp(static_cast<std::streamoff>(event_record_bytes + 48));
        repaired.put(static_cast<char>(second_bytes[48]));
        CHECK(repaired.good());
    }
    CHECK(altered_last.recover());
    CHECK(altered_last.append(third_event));
    const auto repaired_page = altered_last.page(0, 50);
    CHECK(repaired_page.available && repaired_page.events.size() == 3 &&
          repaired_page.events.back().sequence == 3);

    // An oversized sparse journal must fail recovery before allocating or
    // reading its contents. Keep all bytes intact for offline diagnosis.
    const std::string sparse_path = std::string(event_directory) + "/sparse-oversized.bin";
    {
        std::ofstream sparse(sparse_path, std::ios::binary);
        CHECK(sparse.good());
    }
    constexpr uintmax_t sparse_bytes = 32u * 1024u * 1024u;
    std::filesystem::resize_file(sparse_path, sparse_bytes);
    EventStore sparse_journal(sparse_path, 4 * event_record_bytes);
    CHECK(!sparse_journal.recover());
    CHECK(!sparse_journal.page(0, 1).available);
    CHECK(std::filesystem::file_size(sparse_path) == sparse_bytes);
    CHECK(std::filesystem::remove(sparse_path));

    // A journal with the maximum possible durable sequence must not compact
    // away retained events before discovering that the next ID would wrap.
    const std::string exhausted_path = std::string(event_directory) + "/exhausted.bin";
    {
        std::ofstream out(exhausted_path, std::ios::binary);
        for (const auto sequence : {std::numeric_limits<uint64_t>::max() - 1,
                                    std::numeric_limits<uint64_t>::max()}) {
            auto serialized = event;
            serialized.sequence = sequence;
            const auto encoded = encode_event(serialized);
            CHECK(encoded.size() == event_record_bytes);
            out.write(reinterpret_cast<const char *>(encoded.data()), encoded.size());
        }
        CHECK(out.good());
    }
    EventStore exhausted(exhausted_path, 2 * event_record_bytes);
    CHECK(exhausted.recover());
    const auto before_exhaustion = exhausted.page(0, 50);
    CHECK(before_exhaustion.available && before_exhaustion.events.size() == 2 &&
          before_exhaustion.events.back().sequence == std::numeric_limits<uint64_t>::max());
    auto exhausted_next = event;
    exhausted_next.uptime_ms += 2000;
    CHECK(!exhausted.append(exhausted_next));
    CHECK(!std::filesystem::exists(exhausted_path + ".tmp"));
    CHECK(std::filesystem::file_size(exhausted_path) == 2 * event_record_bytes);
    const auto after_exhaustion = exhausted.page(0, 50);
    CHECK(after_exhaustion.available && after_exhaustion.events.size() == 2 &&
          after_exhaustion.events[0].sequence == std::numeric_limits<uint64_t>::max() - 1 &&
          after_exhaustion.events[1].sequence == std::numeric_limits<uint64_t>::max());
    CHECK(!exhausted.append(exhausted_next));

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
    // A CRC-valid edit to an earlier record must not be exposed even when
    // the last record, file identity, size, and sequence order remain intact.
    const std::string early_path = std::string(event_directory) + "/early-mutation.bin";
    EventStore early_events(early_path, 3 * event_record_bytes);
    CHECK(early_events.recover());
    auto early_event = event;
    early_event.kind = EventKind::sensor_state;
    early_event.uptime_ms = 1000;
    CHECK(early_events.append(early_event));
    early_event.uptime_ms = 3000;
    CHECK(early_events.append(early_event));
    early_event.uptime_ms = 5000;
    CHECK(early_events.append(early_event));
    CHECK(early_events.page(0, 1).available);
    auto original_first = early_event;
    original_first.sequence = 1;
    original_first.uptime_ms = 1000;
    auto modified_first = original_first;
    modified_first.kind = EventKind::wifi_state;
    const auto original_record = encode_event(original_first);
    const auto modified_record = encode_event(modified_first);
    CHECK(original_record.size() == event_record_bytes &&
          modified_record.size() == event_record_bytes && original_record != modified_record);
    {
        std::fstream edit(early_path, std::ios::binary | std::ios::in | std::ios::out);
        CHECK(edit.good());
        edit.write(reinterpret_cast<const char *>(modified_record.data()), modified_record.size());
        CHECK(edit.good());
    }
    CHECK(!early_events.page(0, 1).available);
    CHECK(!early_events.page(0, 50).available);
    early_event.uptime_ms = 7000;
    CHECK(!early_events.append(early_event)); // No compaction of altered history.
    CHECK(std::filesystem::file_size(early_path) == 3 * event_record_bytes);
    {
        std::fstream restore(early_path, std::ios::binary | std::ios::in | std::ios::out);
        CHECK(restore.good());
        restore.write(reinterpret_cast<const char *>(original_record.data()),
                      original_record.size());
        CHECK(restore.good());
    }
    CHECK(!early_events.append(early_event)); // Failed append stays quarantined.
    CHECK(early_events.recover());
    CHECK(early_events.page(0, 50).events.size() == 3);
    CHECK(early_events.append(early_event)); // Compaction refreshes fingerprint.
    const auto after_early_compact = early_events.page(0, 50);
    CHECK(after_early_compact.available && after_early_compact.events.size() == 2 &&
          after_early_compact.events[0].sequence == 3 &&
          after_early_compact.events[1].sequence == 4);
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
