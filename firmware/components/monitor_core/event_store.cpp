#include "event_store.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>

namespace monitor {
namespace {
constexpr uint8_t magic[4] = {'T', 'M', 'E', '1'};
uint32_t crc32(const uint8_t *bytes, size_t length) {
    uint32_t crc = ~0u;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
void put(std::vector<uint8_t> &bytes, uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i)
        bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
uint64_t get(const std::vector<uint8_t> &bytes, size_t offset, unsigned width) {
    uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i)
        value |= uint64_t(bytes[offset + i]) << (i * 8);
    return value;
}
// An existing journal may have been replaced after recovery. On POSIX
// reject symlinks without following them; ESP-IDF LittleFS has no lstat().
bool safe_journal_path(const std::string &path) {
    struct stat info {};
#ifdef ESP_PLATFORM
    const int status = stat(path.c_str(), &info);
#else
    const int status = lstat(path.c_str(), &info);
#endif
    return status == 0 && S_ISREG(info.st_mode);
}
#ifndef ESP_PLATFORM
bool same_journal_file(const std::string &path, uint64_t device, uint64_t inode) {
    struct stat info {};
    return lstat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
           static_cast<uint64_t>(info.st_dev) == device &&
           static_cast<uint64_t>(info.st_ino) == inode;
}
#endif
// Detect in-place journal truncation/growth even when the path and inode are
// unchanged. In particular, duplicate suppression must not report success
// after another writer has removed the durable last event.
bool expected_journal_size(const std::string &path, size_t expected) {
    struct stat info {};
#ifdef ESP_PLATFORM
    const int status = stat(path.c_str(), &info);
#else
    const int status = lstat(path.c_str(), &info);
#endif
    return status == 0 && S_ISREG(info.st_mode) && info.st_size >= 0 &&
           static_cast<uint64_t>(info.st_size) == expected;
}
// Detect same-size, CRC-valid edits to earlier durable records. This FNV-1a
// fingerprint detects accidental changes; it is not cryptographic authentication.
constexpr uint64_t fingerprint_basis = 14695981039346656037ull;
uint64_t extend_fingerprint(uint64_t fingerprint, const uint8_t *bytes, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        fingerprint ^= bytes[i];
        fingerprint *= 1099511628211ull;
    }
    return fingerprint;
}
bool expected_journal_fingerprint(const std::string &path, size_t length, uint64_t expected) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    std::array<uint8_t, 4096> buffer{};
    uint64_t fingerprint = fingerprint_basis;
    while (length) {
        const size_t count = std::min(length, buffer.size());
        input.read(reinterpret_cast<char *>(buffer.data()), count);
        if (input.gcount() != static_cast<std::streamsize>(count))
            return false;
        fingerprint = extend_fingerprint(fingerprint, buffer.data(), count);
        length -= count;
    }
    return !input.bad() && fingerprint == expected;
}
// Verify the durable final record before duplicate suppression or another
// append. File size and inode alone cannot detect same-length corruption.
bool expected_last_record(const std::string &path, size_t expected_bytes,
                          const std::optional<JournalEvent> &last_event) {
    if (expected_bytes == 0)
        return !last_event;
    if (!last_event || expected_bytes < event_record_bytes ||
        expected_bytes % event_record_bytes != 0)
        return false;
    const auto expected = encode_event(*last_event);
    if (expected.size() != event_record_bytes)
        return false;
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    input.seekg(static_cast<std::streamoff>(expected_bytes - event_record_bytes));
    if (!input)
        return false;
    std::array<uint8_t, event_record_bytes> actual{};
    input.read(reinterpret_cast<char *>(actual.data()), actual.size());
    return input.gcount() == static_cast<std::streamsize>(actual.size()) &&
           std::equal(actual.begin(), actual.end(), expected.begin());
}
bool valid_event(const JournalEvent &event) {
    return event.uptime_ms >= 0 &&
           static_cast<unsigned>(event.kind) <= static_cast<unsigned>(EventKind::storage_error) &&
           static_cast<unsigned>(event.previous) <= static_cast<unsigned>(State::bus_fault) &&
           static_cast<unsigned>(event.state) <= static_cast<unsigned>(State::bus_fault) &&
           (!event.rom || valid_rom(event.rom)) && (!event.utc_seconds || *event.utc_seconds >= 0);
}
std::vector<JournalEvent> read_all(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    std::vector<JournalEvent> events;
    uint64_t previous_sequence = 0;
    std::array<uint8_t, event_record_bytes> buffer{};
    while (input.read(reinterpret_cast<char *>(buffer.data()), buffer.size())) {
        auto event = decode_event({buffer.begin(), buffer.end()});
        if (!event || event->sequence <= previous_sequence)
            return {};
        previous_sequence = event->sequence;
        events.push_back(*event);
    }
    if (input.bad() ||
        (input.gcount() != 0 && input.gcount() != static_cast<std::streamsize>(buffer.size())))
        return {};
    return events;
}
} // namespace

std::vector<uint8_t> encode_event(const JournalEvent &event) {
    if (!valid_event(event) || !event.sequence)
        return {};
    std::vector<uint8_t> bytes(magic, magic + sizeof(magic));
    put(bytes, 1, 2);
    put(bytes, event_record_bytes, 2);
    put(bytes, event.sequence, 8);
    put(bytes, event.boot_id, 8);
    put(bytes, static_cast<uint64_t>(event.uptime_ms), 8);
    put(bytes, event.utc_seconds.value_or(0), 8);
    put(bytes, event.rom, 8);
    put(bytes, static_cast<uint8_t>(event.kind), 1);
    put(bytes, static_cast<uint8_t>(event.previous), 1);
    put(bytes, static_cast<uint8_t>(event.state), 1);
    put(bytes, event.utc_seconds.has_value(), 1);
    put(bytes, event.detail, 4);
    put(bytes, crc32(bytes.data(), bytes.size()), 4);
    return bytes;
}

std::optional<JournalEvent> decode_event(const std::vector<uint8_t> &bytes) {
    if (bytes.size() != event_record_bytes ||
        !std::equal(std::begin(magic), std::end(magic), bytes.begin()) || get(bytes, 4, 2) != 1 ||
        get(bytes, 6, 2) != event_record_bytes || bytes[51] > 1 ||
        get(bytes, 56, 4) != crc32(bytes.data(), bytes.size() - 4))
        return std::nullopt;
    JournalEvent event{};
    event.sequence = get(bytes, 8, 8);
    event.boot_id = get(bytes, 16, 8);
    event.uptime_ms = static_cast<int64_t>(get(bytes, 24, 8));
    if (bytes[51])
        event.utc_seconds = static_cast<int64_t>(get(bytes, 32, 8));
    else if (get(bytes, 32, 8) != 0)
        return std::nullopt;
    event.rom = get(bytes, 40, 8);
    event.kind = static_cast<EventKind>(bytes[48]);
    event.previous = static_cast<State>(bytes[49]);
    event.state = static_cast<State>(bytes[50]);
    event.detail = static_cast<uint32_t>(get(bytes, 52, 4));
    if (!event.sequence || !valid_event(event))
        return std::nullopt;
    return event;
}

EventStore::EventStore(std::string path, size_t max_bytes)
    : path_(std::move(path)), max_bytes_(max_bytes) {}

bool EventStore::recover() {
    // Any failed recovery invalidates the writer, including an already-open
    // instance. Never append to a journal whose integrity was not verified.
    append_ready_ = false;
    expected_bytes_ = 0;
    journal_fingerprint_ = fingerprint_basis;
#ifndef ESP_PLATFORM
    journal_dev_ = 0;
    journal_ino_ = 0;
#endif
    if (max_bytes_ < 2 * event_record_bytes)
        return false;
    // A failed read is not proof that the journal is absent. In particular,
    // fopen("wb") here would destroy a writeable but unreadable journal.
    // Refuse symlinks/directories and only create a genuinely missing file.
    struct stat metadata {};
    // ESP-IDF's VFS does not export lstat(). Its LittleFS implementation
    // does not support symlinks; on POSIX hosts use lstat() to reject links.
#ifdef ESP_PLATFORM
    const int status = stat(path_.c_str(), &metadata);
#else
    const int status = lstat(path_.c_str(), &metadata);
#endif
    if (status != 0) {
        if (errno != ENOENT)
            return false;
        const int fd = open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0)
            return false;
        const bool synced = fsync(fd) == 0;
        const bool closed = close(fd) == 0;
        if (!synced || !closed)
            return false;
#ifndef ESP_PLATFORM
        struct stat created {};
        if (lstat(path_.c_str(), &created) != 0 || !S_ISREG(created.st_mode))
            return false;
        journal_dev_ = static_cast<uint64_t>(created.st_dev);
        journal_ino_ = static_cast<uint64_t>(created.st_ino);
#endif
        last_sequence_ = 0;
        last_event_.reset();
        expected_bytes_ = 0;
        journal_fingerprint_ = fingerprint_basis;
        append_ready_ = true;
        return true;
    }
    if (!S_ISREG(metadata.st_mode))
        return false;
    // The journal is bounded by a small flash partition, but even a corrupt
    // oversized file must not allocate its entire contents in device RAM.
    // Stream and validate fixed-size records before modifying a torn tail.
    if (metadata.st_size < 0 || static_cast<uint64_t>(metadata.st_size) > max_bytes_)
        return false;
    const size_t file_bytes = static_cast<size_t>(metadata.st_size);
    const size_t complete_bytes = file_bytes / event_record_bytes * event_record_bytes;
    std::ifstream input(path_, std::ios::binary);
    if (!input)
        return false;
    uint64_t previous_sequence = 0;
    uint64_t fingerprint = fingerprint_basis;
    std::optional<JournalEvent> recovered_last_event;
    std::array<uint8_t, event_record_bytes> record{};
    for (size_t offset = 0; offset < complete_bytes; offset += event_record_bytes) {
        input.read(reinterpret_cast<char *>(record.data()), record.size());
        if (input.gcount() != static_cast<std::streamsize>(record.size()) || input.bad())
            return false;
        auto event = decode_event({record.begin(), record.end()});
        if (!event || event->sequence <= previous_sequence)
            return false;
        previous_sequence = event->sequence;
        recovered_last_event = *event;
        fingerprint = extend_fingerprint(fingerprint, record.data(), record.size());
    }
    const size_t remaining = file_bytes - complete_bytes;
    std::array<uint8_t, event_record_bytes> tail{};
    if (remaining) {
        input.read(reinterpret_cast<char *>(tail.data()), remaining);
        if (input.gcount() != static_cast<std::streamsize>(remaining) || input.bad())
            return false;
        // Only a prefix of the current record header is an interrupted
        // append. Unknown format/version/length is preserved for diagnosis.
        static_assert(event_record_bytes == 60);
        const uint8_t expected_header[] = {'T', 'M', 'E', '1', 1, 0, 60, 0};
        if (std::memcmp(tail.data(), expected_header,
                        std::min(remaining, sizeof(expected_header))) != 0)
            return false;
    }
    // A file that grew while being read is not the validated snapshot.
    if (input.peek() != std::char_traits<char>::eof() || input.bad())
        return false;
    const uint64_t original_fingerprint = extend_fingerprint(fingerprint, tail.data(), remaining);
#ifndef ESP_PLATFORM
    struct stat recovered {};
    if (lstat(path_.c_str(), &recovered) != 0 || !S_ISREG(recovered.st_mode) ||
        recovered.st_dev != metadata.st_dev || recovered.st_ino != metadata.st_ino)
        return false;
#endif
    // Recheck the full pre-recovery image (including the torn tail) before
    // truncation. A read error or concurrent replacement must not destroy it.
    if (!expected_journal_size(path_, file_bytes) ||
        !expected_journal_fingerprint(path_, file_bytes, original_fingerprint))
        return false;
    if (remaining && truncate(path_.c_str(), static_cast<off_t>(complete_bytes)) != 0)
        return false;
#ifndef ESP_PLATFORM
    // Bind subsequent appends to the exact regular file we recovered.
    if (lstat(path_.c_str(), &recovered) != 0 || !S_ISREG(recovered.st_mode) ||
        recovered.st_dev != metadata.st_dev || recovered.st_ino != metadata.st_ino)
        return false;
    journal_dev_ = static_cast<uint64_t>(recovered.st_dev);
    journal_ino_ = static_cast<uint64_t>(recovered.st_ino);
#endif
    if (!expected_journal_size(path_, complete_bytes) ||
        !expected_journal_fingerprint(path_, complete_bytes, fingerprint))
        return false;
    last_sequence_ = previous_sequence;
    last_event_ = recovered_last_event;
    expected_bytes_ = complete_bytes;
    journal_fingerprint_ = fingerprint;
    append_ready_ = true;
    return true;
}

bool EventStore::compact() {
    if (!safe_journal_path(path_))
        return false;
#ifndef ESP_PLATFORM
    if (!same_journal_file(path_, journal_dev_, journal_ino_))
        return false;
#endif
    // Recovery rejects oversized journals. Never silently compact one into
    // apparently valid data or advance sequences from stale in-memory state.
    if (bytes() > max_bytes_)
        return false;
    auto events = read_all(path_);
    if (events.empty() ||
        !expected_journal_fingerprint(path_, expected_bytes_, journal_fingerprint_))
        return false;
#ifndef ESP_PLATFORM
    if (!same_journal_file(path_, journal_dev_, journal_ino_))
        return false;
#endif
    const size_t keep = std::max<size_t>(1, events.size() / 2);
    const std::string temporary = path_ + ".tmp";
    // Never truncate an existing .tmp path, including a symlink or a
    // preserved interrupted-compaction artifact. A failed compaction
    // leaves the current journal untouched and disables future appends.
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return false;
    FILE *output = fdopen(fd, "wb");
    if (!output) {
        close(fd);
        std::remove(temporary.c_str());
        return false;
    }
    bool ok = true;
    uint64_t compacted_fingerprint = fingerprint_basis;
    for (auto it = events.end() - keep; it != events.end(); ++it) {
        auto bytes = encode_event(*it);
        if (bytes.size() != event_record_bytes ||
            std::fwrite(bytes.data(), 1, bytes.size(), output) != bytes.size()) {
            ok = false;
            break;
        }
        compacted_fingerprint =
            extend_fingerprint(compacted_fingerprint, bytes.data(), bytes.size());
    }
    if (std::fflush(output) != 0 || fsync(fileno(output)) != 0)
        ok = false;
    if (std::fclose(output) != 0)
        ok = false;
    if (!ok || !expected_journal_size(path_, expected_bytes_) ||
        !expected_journal_fingerprint(path_, expected_bytes_, journal_fingerprint_) ||
        std::rename(temporary.c_str(), path_.c_str()) != 0) {
        std::remove(temporary.c_str());
        return false;
    }
#ifndef ESP_PLATFORM
    // Compaction atomically replaces the inode: bind subsequent appends to it.
    struct stat compacted {};
    if (lstat(path_.c_str(), &compacted) != 0 || !S_ISREG(compacted.st_mode))
        return false;
    journal_dev_ = static_cast<uint64_t>(compacted.st_dev);
    journal_ino_ = static_cast<uint64_t>(compacted.st_ino);
#endif
    expected_bytes_ = keep * event_record_bytes;
    journal_fingerprint_ = compacted_fingerprint;
    return true;
}

bool EventStore::append(const JournalEvent &input) {
    if (!append_ready_ || !valid_event(input))
        return false;
    if (!safe_journal_path(path_)) {
        append_ready_ = false;
        return false;
    }
#ifndef ESP_PLATFORM
    if (!same_journal_file(path_, journal_dev_, journal_ino_)) {
        append_ready_ = false;
        return false;
    }
#endif
    if (!expected_journal_size(path_, expected_bytes_) ||
        !expected_last_record(path_, expected_bytes_, last_event_) ||
        !expected_journal_fingerprint(path_, expected_bytes_, journal_fingerprint_)) {
        // A same-size mutation of the final durable record must not be
        // mistaken for a valid duplicate or extended with a new sequence.
        append_ready_ = false;
        return false;
    }
    if (last_event_ && input.boot_id == last_event_->boot_id &&
        input.uptime_ms >= last_event_->uptime_ms &&
        input.uptime_ms - last_event_->uptime_ms < 1000 && input.kind == last_event_->kind &&
        input.rom == last_event_->rom && input.previous == last_event_->previous &&
        input.state == last_event_->state && input.utc_seconds == last_event_->utc_seconds &&
        input.detail == last_event_->detail)
        return true;
    if (bytes() + event_record_bytes > max_bytes_ && !compact()) {
        // Compaction can fail because the existing journal is corrupt. Require
        // a fresh successful recovery even if the file is repaired later.
        append_ready_ = false;
        return false;
    }
    JournalEvent event = input;
    event.sequence = last_sequence_ + 1;
    auto encoded = encode_event(event);
    if (encoded.empty())
        return false;
    int flags = O_WRONLY | O_APPEND;
#if !defined(ESP_PLATFORM) && defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    const int fd = open(path_.c_str(), flags);
    if (fd < 0) {
        append_ready_ = false;
        return false;
    }
    struct stat opened {};
    if (fstat(fd, &opened) != 0 || !S_ISREG(opened.st_mode)) {
        close(fd);
        append_ready_ = false;
        return false;
    }
#ifndef ESP_PLATFORM
    if (static_cast<uint64_t>(opened.st_dev) != journal_dev_ ||
        static_cast<uint64_t>(opened.st_ino) != journal_ino_) {
        close(fd);
        append_ready_ = false;
        return false;
    }
#endif
    FILE *output = fdopen(fd, "ab");
    if (!output) {
        close(fd);
        append_ready_ = false;
        return false;
    }
    bool ok = std::fwrite(encoded.data(), 1, encoded.size(), output) == encoded.size();
    if (std::fflush(output) != 0 || fsync(fileno(output)) != 0)
        ok = false;
    if (std::fclose(output) != 0)
        ok = false;
    if (!ok) {
        append_ready_ = false;
        return false;
    }
#ifndef ESP_PLATFORM
    if (!same_journal_file(path_, journal_dev_, journal_ino_)) {
        append_ready_ = false;
        return false;
    }
#endif
    if (!expected_journal_size(path_, expected_bytes_ + event_record_bytes)) {
        append_ready_ = false;
        return false;
    }
    expected_bytes_ += event_record_bytes;
    journal_fingerprint_ = extend_fingerprint(journal_fingerprint_, encoded.data(), encoded.size());
    last_sequence_ = event.sequence;
    last_event_ = event;
    return true;
}

JournalPage EventStore::page(uint64_t after, size_t limit) const {
    JournalPage page{};
    // A journal is not API-readable until recovery has validated it. A failed
    // append/recovery also quarantines reads until another successful recovery.
    if (!append_ready_ || !limit || limit > 50 || !safe_journal_path(path_) ||
        !expected_journal_size(path_, expected_bytes_) ||
        !expected_last_record(path_, expected_bytes_, last_event_) || bytes() > max_bytes_)
        return page;
#ifndef ESP_PLATFORM
    if (!same_journal_file(path_, journal_dev_, journal_ino_))
        return page;
#endif
    std::ifstream input(path_, std::ios::binary);
    if (!input)
        return page;
    page.available = true;
    uint64_t previous_sequence = 0;
    uint64_t fingerprint = fingerprint_basis;
    std::array<uint8_t, event_record_bytes> buffer{};
    while (input.read(reinterpret_cast<char *>(buffer.data()), buffer.size())) {
        fingerprint = extend_fingerprint(fingerprint, buffer.data(), buffer.size());
        auto event = decode_event({buffer.begin(), buffer.end()});
        if (!event || event->sequence <= previous_sequence)
            return {};
        previous_sequence = event->sequence;
        if (!page.oldest_available)
            page.oldest_available = event->sequence;
        if (event->sequence <= after)
            continue;
        if (page.events.size() == limit) {
            // A page boundary is not an integrity boundary: validate the
            // remaining journal before exposing even the first page.
            page.has_more = true;
            continue;
        }
        page.events.push_back(*event);
        page.next_after = event->sequence;
    }
    if (input.bad() ||
        (input.gcount() != 0 && input.gcount() != static_cast<std::streamsize>(buffer.size())))
        return {};
    // Reject an in-place change that occurred while the page was scanned.
    // A valid CRC alone cannot prove the final event is the recovered one.
    if (fingerprint != journal_fingerprint_ || !expected_journal_size(path_, expected_bytes_) ||
        !expected_last_record(path_, expected_bytes_, last_event_))
        return {};
    if (page.events.empty())
        page.next_after = after;
#ifndef ESP_PLATFORM
    if (!same_journal_file(path_, journal_dev_, journal_ino_))
        return {};
#endif
    return page;
}

size_t EventStore::bytes() const {
    std::ifstream input(path_, std::ios::binary | std::ios::ate);
    return input ? static_cast<size_t>(input.tellg()) : 0;
}
} // namespace monitor
