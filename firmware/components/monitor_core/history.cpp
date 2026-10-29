#include "history.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
namespace monitor {
namespace {
uint32_t crc32(const uint8_t *data, std::size_t length) {
    uint32_t crc = ~0u;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
void put(std::vector<uint8_t> &out, uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i)
        out.push_back((value >> (i * 8)) & 0xff);
}
uint64_t get(const std::vector<uint8_t> &in, std::size_t at, unsigned width) {
    uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i)
        value |= uint64_t(in.at(at + i)) << (i * 8);
    return value;
}
std::size_t file_size(const std::string &path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 ? info.st_size : 0;
}
// Refuse a host symlink substituted for the history directory itself.
// ESP-IDF LittleFS has no symlinks or lstat().
bool safe_history_directory(const std::string &path) {
    struct stat info {};
#ifdef ESP_PLATFORM
    const int status = stat(path.c_str(), &info);
#else
    const int status = lstat(path.c_str(), &info);
#endif
    return status == 0 && S_ISDIR(info.st_mode);
}
// A callback can rename the history directory and install a different,
// ordinary directory at the same path. Host exports bind to the original
// device/inode identity. ESP-IDF LittleFS has no reliable inode identity.
bool same_history_directory(const std::string &path, const struct stat &expected) {
    if (!safe_history_directory(path))
        return false;
#ifndef ESP_PLATFORM
    struct stat current {};
    return lstat(path.c_str(), &current) == 0 && current.st_dev == expected.st_dev &&
           current.st_ino == expected.st_ino;
#else
    (void)expected;
    return true;
#endif
}
// A path that was a regular segment during recovery may have been replaced
// before a later read/write. Do not follow host symlinks or treat a directory
// as a valid segment. ESP-IDF LittleFS has no symlink support or lstat().
bool safe_segment_path(const std::string &path, bool allow_missing) {
    struct stat info {};
#ifdef ESP_PLATFORM
    const int status = stat(path.c_str(), &info);
#else
    const int status = lstat(path.c_str(), &info);
#endif
    if (status == 0)
        return S_ISREG(info.st_mode);
    return allow_missing && errno == ENOENT;
}
// An existing segment can be truncated or extended without changing its
// pathname or inode. Refuse to append until the recovered byte count matches.
bool segment_has_size(const std::string &path, std::size_t expected) {
    struct stat info {};
#ifdef ESP_PLATFORM
    const int status = stat(path.c_str(), &info);
#else
    const int status = lstat(path.c_str(), &info);
#endif
    return status == 0 && S_ISREG(info.st_mode) && info.st_size >= 0 &&
           static_cast<uint64_t>(info.st_size) == expected;
}
// A callback may replace a valid same-name segment without changing the
// parent directory or byte count. On POSIX, retain the original file identity
// for the duration of each export; LittleFS lacks stable inode identifiers.
bool same_history_segment(const std::string &path, const struct stat &expected) {
    if (!safe_segment_path(path, false))
        return false;
#ifndef ESP_PLATFORM
    struct stat current {};
    return lstat(path.c_str(), &current) == 0 && current.st_dev == expected.st_dev &&
           current.st_ino == expected.st_ino;
#else
    (void)expected;
    return true;
#endif
}
// An export callback can edit an earlier record in place while preserving
// its inode, size, and per-record CRC. Compare the complete segment before
// and after traversal to avoid reporting a mixed-time export as successful.
// This is accidental-change detection, not cryptographic authentication.
bool segment_fingerprint(const std::string &path, std::size_t expected_bytes, uint64_t &result) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    std::array<char, 4096> buffer{};
    uint64_t fingerprint = 14695981039346656037ull;
    for (std::size_t remaining = expected_bytes; remaining;) {
        const auto count = std::min(remaining, buffer.size());
        input.read(buffer.data(), static_cast<std::streamsize>(count));
        if (input.gcount() != static_cast<std::streamsize>(count) || input.bad())
            return false;
        for (std::size_t index = 0; index < count; ++index) {
            fingerprint ^= static_cast<unsigned char>(buffer[index]);
            fingerprint *= 1099511628211ull;
        }
        remaining -= count;
    }
    if (input.peek() != std::char_traits<char>::eof() || input.bad())
        return false;
    result = fingerprint;
    return true;
}
bool unchanged_segment_contents(const std::string &path, std::size_t size,
                                uint64_t expected_fingerprint) {
    uint64_t actual_fingerprint = 0;
    return segment_has_size(path, size) && segment_fingerprint(path, size, actual_fingerprint) &&
           actual_fingerprint == expected_fingerprint && segment_has_size(path, size);
}
// Read one bounded record. Truncated/corrupt records stop scanning at their offset.
std::optional<Bucket> next(std::istream &in) {
    std::vector<uint8_t> bytes(8);
    if (!in.read(reinterpret_cast<char *>(bytes.data()), 8))
        return std::nullopt;
    auto size = get(bytes, 6, 2);
    if (size < 40 || size > max_history_record_bytes)
        return std::nullopt;
    bytes.resize(size);
    if (!in.read(reinterpret_cast<char *>(bytes.data() + 8), size - 8))
        return std::nullopt;
    return decode_bucket(bytes);
}
} // namespace
std::vector<uint8_t> encode_bucket(const Bucket &bucket) {
    if (bucket.sensors.size() > max_sensors || bucket.start_uptime_ms < 0 ||
        (bucket.utc_seconds && *bucket.utc_seconds < 0))
        return {};
    std::vector<uint8_t> out{'T', 'M', 'H', '2'};
    put(out, 2, 2);
    size_t length = 40;
    for (const auto &a : bucket.sensors)
        length += 27 + a.name.size();
    if (length > max_history_record_bytes)
        return {};
    put(out, length, 2);
    put(out, bucket.boot_id, 8);
    put(out, bucket.start_uptime_ms, 8);
    put(out, bucket.utc_seconds.value_or(0), 8);
    put(out, bucket.sensors.size(), 1);
    put(out, bucket.utc_seconds.has_value(), 1);
    put(out, 0, 2);
    std::vector<uint64_t> identities;
    for (const auto &a : bucket.sensors) {
        Settings label;
        label.name = a.name.empty() ? "Unknown" : a.name;
        if (!valid_settings(label) || (a.state_flags & ~uint16_t(0x1ff)) ||
            (!a.count && (!a.partial || a.average != 0 || a.minimum != 0 || a.maximum != 0)) ||
            !valid_rom(a.rom) || a.count > 65535 || !std::isfinite(a.average) ||
            !std::isfinite(a.minimum) || !std::isfinite(a.maximum) || a.minimum < -75 ||
            a.maximum > 145 || a.minimum > a.average || a.average > a.maximum ||
            std::find(identities.begin(), identities.end(), a.rom) != identities.end())
            return {};
        identities.push_back(a.rom);
        put(out, a.rom, 8);
        for (double value : {a.average, a.minimum, a.maximum})
            put(out, static_cast<uint32_t>(static_cast<int32_t>(std::lround(value * 1000))), 4);
        put(out, a.count, 2);
        put(out, a.partial, 1);
        put(out, 0, 1);
        put(out, a.state_flags, 2);
        put(out, a.name.size(), 1);
        out.insert(out.end(), a.name.begin(), a.name.end());
    }
    put(out, crc32(out.data(), out.size()), 4);
    return out;
}
std::optional<Bucket> decode_bucket(const std::vector<uint8_t> &in) {
    if (in.size() < 40 || in.size() > max_history_record_bytes)
        return std::nullopt;
    const bool legacy = std::memcmp(in.data(), "TMH1", 4) == 0 && get(in, 4, 2) == 1;
    const bool current = std::memcmp(in.data(), "TMH2", 4) == 0 && get(in, 4, 2) == 2;
    if ((!legacy && !current) || get(in, 6, 2) != in.size() || in[32] > 5 || in[33] > 1 || in[34] ||
        in[35] || get(in, in.size() - 4, 4) != crc32(in.data(), in.size() - 4))
        return std::nullopt;
    // An unsynchronized bucket must not carry a hidden UTC timestamp.
    // A synchronized bucket must not expose a negative/unrepresentable UTC.
    const uint64_t encoded_utc = get(in, 24, 8);
    if ((!in[33] && encoded_utc != 0) || (in[33] && (encoded_utc >> 63) != 0))
        return std::nullopt;
    Bucket bucket{get(in, 8, 8), static_cast<int64_t>(get(in, 16, 8)), std::nullopt, {}};
    if (in[33])
        bucket.utc_seconds = static_cast<int64_t>(encoded_utc);
    size_t pos = 36;
    const size_t end = in.size() - 4;
    for (unsigned i = 0; i < in[32]; ++i) {
        if (pos + (legacy ? 24 : 27) > end || in[pos + 22] > 1 || in[pos + 23])
            return std::nullopt;
        auto value = [&](size_t at) { return static_cast<int32_t>(get(in, at, 4)) / 1000.0; };
        Aggregate aggregate{get(in, pos, 8),
                            value(pos + 8),
                            value(pos + 12),
                            value(pos + 16),
                            unsigned(get(in, pos + 20, 2)),
                            bool(in[pos + 22])};
        if (legacy && !aggregate.count)
            return std::nullopt;
        if (current) {
            const size_t length = in[pos + 26];
            if (length > 32 || pos + 27 + length > end)
                return std::nullopt;
            aggregate.state_flags = uint16_t(get(in, pos + 24, 2));
            aggregate.name.assign(reinterpret_cast<const char *>(in.data() + pos + 27), length);
            pos += 27 + length;
        } else
            pos += 24;
        bucket.sensors.push_back(aggregate);
    }
    if (pos != end || encode_bucket(bucket).empty())
        return std::nullopt;
    return bucket;
}
HistoryStore::HistoryStore(std::string directory, std::size_t max_bytes, std::size_t segment_bytes)
    : directory_(std::move(directory)), max_bytes_(max_bytes), segment_bytes_(segment_bytes) {}
std::string HistoryStore::path(unsigned number) const {
    std::ostringstream name;
    name << directory_ << "/" << std::setw(8) << std::setfill('0') << number << ".tmh";
    return name.str();
}
std::vector<unsigned> HistoryStore::segments() const {
    std::vector<unsigned> values;
    if (!safe_history_directory(directory_))
        return values;
    auto *dir = opendir(directory_.c_str());
    if (!dir)
        return values;
    while (auto *entry = readdir(dir)) {
        std::string name(entry->d_name);
        if (name.size() == 12 && name.substr(8) == ".tmh" &&
            std::all_of(name.begin(), name.begin() + 8,
                        [](char c) { return c >= '0' && c <= '9'; }))
            values.push_back(std::stoul(name.substr(0, 8)));
    }
    closedir(dir);
    std::sort(values.begin(), values.end());
    return values;
}
bool HistoryStore::recover() {
    // A failed recheck must revoke append permission on reused instances.
    // In-memory segment selection is not trustworthy until recovery succeeds.
    append_ready_ = false;
    active_size_ = 0;
    active_fingerprint_ = 14695981039346656037ull;
    if (mkdir(directory_.c_str(), 0700) != 0) {
        struct stat info {};
        if (stat(directory_.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
            return false;
    }
    // A symlinked or missing history directory must not be treated as an
    // empty, writable store.
    if (!safe_history_directory(directory_))
        return false;
    // A failed directory enumeration is not an empty history.
    auto *dir = opendir(directory_.c_str());
    if (!dir)
        return false;
    closedir(dir);
    auto files = segments();
    active_ = files.empty() ? 0 : files.back();
    for (auto number : files) {
        if (!safe_segment_path(path(number), false))
            return false;
        if (file_size(path(number)) > segment_bytes_ + max_history_record_bytes)
            return false;
        std::ifstream input(path(number), std::ios::binary);
        if (!input)
            return false;
        std::size_t good = 0;
        while (auto bucket = next(input))
            good = static_cast<std::size_t>(input.tellg());
        if (input.bad())
            return false;
        if (good != file_size(path(number))) {
            // Only the active tail may be repaired. Never alter completed segments.
            if (number != active_)
                return false;
            input.clear();
            input.seekg(static_cast<std::streamoff>(good));
            std::array<char, 8> header{};
            input.read(header.data(), header.size());
            // Only a prefix of a recognized TMH1/TMH2 header can be
            // truncated as an interrupted append. Preserve unknown magic,
            // future schema versions and impossible record lengths, including
            // prefixes shorter than a complete eight-byte header.
            const auto header_bytes = input.gcount();
            if (input.bad())
                return false;
            const auto remaining = file_size(path(number)) - good;
            if (!header_bytes ||
                std::memcmp(header.data(), "TMH", std::min<std::streamsize>(3, header_bytes)) != 0)
                return false;
            if (header_bytes >= 4 && header[3] != '1' && header[3] != '2')
                return false;
            if (header_bytes >= 5 && header[4] != (header[3] == '1' ? 1 : 2))
                return false;
            if (header_bytes >= 6 && header[5] != 0)
                return false;
            // A complete but CRC-damaged final record is not an interrupted
            // append. A partial record must declare a valid size greater than
            // its currently persisted bytes.
            if (header_bytes == 8) {
                const size_t low = static_cast<uint8_t>(header[6]);
                const size_t high = static_cast<uint8_t>(header[7]);
                const size_t declared = low | (high << 8);
                if (declared < 40 || declared > max_history_record_bytes || remaining >= declared)
                    return false;
            }
            // Refuse to truncate an interior corrupt record if later valid data exists.
            input.clear();
            input.seekg(static_cast<std::streamoff>(good + 1));
            std::vector<uint8_t> tail((std::istreambuf_iterator<char>(input)), {});
            if (input.bad())
                return false;
            for (std::size_t offset = 0; offset + 40 <= tail.size(); ++offset) {
                if (std::memcmp(tail.data() + offset, "TMH", 3) != 0)
                    continue;
                size_t length = uint16_t(tail[offset + 6]) | (uint16_t(tail[offset + 7]) << 8);
                if (length >= 40 && length <= max_history_record_bytes &&
                    offset + length <= tail.size()) {
                    std::vector<uint8_t> candidate(tail.begin() + offset,
                                                   tail.begin() + offset + length);
                    if (decode_bucket(candidate))
                        return false;
                }
            }
            if (truncate(path(number).c_str(), good) != 0)
                return false;
        }
    }
    active_segment_exists_ = !files.empty();
#ifndef ESP_PLATFORM
    // A different ordinary directory at this pathname requires re-recovery.
    struct stat recovered_directory {};
    if (lstat(directory_.c_str(), &recovered_directory) != 0 ||
        !same_history_directory(directory_, recovered_directory))
        return false;
    directory_dev_ = static_cast<uint64_t>(recovered_directory.st_dev);
    directory_ino_ = static_cast<uint64_t>(recovered_directory.st_ino);
    if (active_segment_exists_) {
        struct stat recovered_active {};
        if (lstat(path(active_).c_str(), &recovered_active) != 0 ||
            !S_ISREG(recovered_active.st_mode))
            return false;
        active_dev_ = static_cast<uint64_t>(recovered_active.st_dev);
        active_ino_ = static_cast<uint64_t>(recovered_active.st_ino);
    } else {
        active_dev_ = 0;
        active_ino_ = 0;
    }
#endif
    if (active_segment_exists_) {
        struct stat active_metadata {};
#ifdef ESP_PLATFORM
        const int status = stat(path(active_).c_str(), &active_metadata);
#else
        const int status = lstat(path(active_).c_str(), &active_metadata);
#endif
        if (status != 0 || !S_ISREG(active_metadata.st_mode) || active_metadata.st_size < 0)
            return false;
        active_size_ = static_cast<std::size_t>(active_metadata.st_size);
        if (!segment_fingerprint(path(active_), active_size_, active_fingerprint_) ||
            !segment_has_size(path(active_), active_size_))
            return false;
    }
    append_ready_ = true;
    return true;
}
std::size_t HistoryStore::bytes() const {
    std::size_t total = 0;
    for (auto number : segments())
        total += file_size(path(number));
    return total;
}
bool HistoryStore::append(const Bucket &bucket) {
    if (!append_ready_)
        return false;
    if (!safe_history_directory(directory_)) {
        append_ready_ = false;
        return false;
    }
#ifndef ESP_PLATFORM
    struct stat current_directory {};
    if (lstat(directory_.c_str(), &current_directory) != 0 ||
        static_cast<uint64_t>(current_directory.st_dev) != directory_dev_ ||
        static_cast<uint64_t>(current_directory.st_ino) != directory_ino_) {
        append_ready_ = false;
        return false;
    }
#endif
    auto data = encode_bucket(bucket);
    if (data.empty() || segment_bytes_ < data.size() || max_bytes_ < 2 * segment_bytes_)
        return false;
    // Validate every retained path before eviction and the active path before
    // append. Do not silently recreate a segment deleted since recovery.
    auto files = segments();
    if ((active_segment_exists_ && (files.empty() || files.back() != active_)) ||
        (!active_segment_exists_ && !files.empty())) {
        append_ready_ = false;
        return false;
    }
    for (auto number : files) {
        if (!safe_segment_path(path(number), false)) {
            append_ready_ = false;
            return false;
        }
    }
    if (!safe_segment_path(path(active_), !active_segment_exists_)) {
        append_ready_ = false;
        return false;
    }
#ifndef ESP_PLATFORM
    // A different ordinary file with the same valid name/size is not the
    // recovered active segment, even when the parent directory is unchanged.
    if (active_segment_exists_) {
        struct stat current_active {};
        if (lstat(path(active_).c_str(), &current_active) != 0 ||
            static_cast<uint64_t>(current_active.st_dev) != active_dev_ ||
            static_cast<uint64_t>(current_active.st_ino) != active_ino_) {
            append_ready_ = false;
            return false;
        }
    }
#endif
    // Detect a same-size, CRC-valid in-place edit since the last recovery or
    // append, including when this write would roll over and evict history.
    if (active_segment_exists_ &&
        !unchanged_segment_contents(path(active_), active_size_, active_fingerprint_)) {
        append_ready_ = false;
        return false;
    }
    bool create_new = !active_segment_exists_;
    if (file_size(path(active_)) + data.size() > segment_bytes_) {
        if (active_ >= 99999999)
            return false;
        ++active_;
        create_new = true;
    }
    // A new segment must be created exclusively: never append to a path
    // substituted between validation and rollover (including a symlink).
    if (create_new && !safe_segment_path(path(active_), true)) {
        append_ready_ = false;
        return false;
    }
    // Do not evict retained history until the replacement record is safely
    // persisted. A failed append (ENOSPC, flush, fsync) must not erase the
    // oldest good segment just to make room for data that never landed.
    std::size_t total = bytes();
    int flags = O_WRONLY | (create_new ? (O_CREAT | O_EXCL) : O_APPEND);
#if !defined(ESP_PLATFORM) && defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    const int fd = open(path(active_).c_str(), flags, 0600);
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
    // Revalidate the opened descriptor: path checks alone leave a rename
    // window between lstat() and open().
    if (!create_new && static_cast<uint64_t>(opened.st_dev) != active_dev_) {
        close(fd);
        append_ready_ = false;
        return false;
    }
    if (!create_new && static_cast<uint64_t>(opened.st_ino) != active_ino_) {
        close(fd);
        append_ready_ = false;
        return false;
    }
#endif
    FILE *output = fdopen(fd, create_new ? "wb" : "ab");
    if (!output) {
        close(fd);
        append_ready_ = false;
        return false;
    }
    bool ok = std::fwrite(data.data(), 1, data.size(), output) == data.size();
    if (std::fflush(output) != 0)
        ok = false;
    if (fsync(fileno(output)) != 0)
        ok = false;
    if (std::fclose(output) != 0)
        ok = false;
    if (!ok) {
        append_ready_ = false;
        return false;
    }
#ifndef ESP_PLATFORM
    // Do not report success if the pathname was replaced while writing.
    struct stat written_active {};
    if (lstat(path(active_).c_str(), &written_active) != 0 ||
        written_active.st_dev != opened.st_dev || written_active.st_ino != opened.st_ino) {
        append_ready_ = false;
        return false;
    }
    active_dev_ = static_cast<uint64_t>(opened.st_dev);
    active_ino_ = static_cast<uint64_t>(opened.st_ino);
#endif
    const auto written_size = (create_new ? 0 : active_size_) + data.size();
    uint64_t written_fingerprint = create_new ? 14695981039346656037ull : active_fingerprint_;
    for (const auto byte : data) {
        written_fingerprint ^= byte;
        written_fingerprint *= 1099511628211ull;
    }
    // Read back the committed active bytes, not only their size. A concurrent
    // in-place edit during the write must not be accepted as a durable append.
    if (!unchanged_segment_contents(path(active_), written_size, written_fingerprint)) {
        append_ready_ = false;
        return false;
    }
    active_size_ = written_size;
    active_fingerprint_ = written_fingerprint;
    active_segment_exists_ = true;
    // Retention may briefly exceed its configured logical budget by one
    // record. Evict only after fsync succeeds, and fail closed if eviction
    // cannot complete. A subsequent recovery can reconcile the segments.
    total += data.size();
    for (auto number : files) {
        if (total <= max_bytes_)
            break;
        if (number == active_ || !safe_segment_path(path(number), false)) {
            append_ready_ = false;
            return false;
        }
        const auto size = file_size(path(number));
        if (std::remove(path(number).c_str()) != 0) {
            append_ready_ = false;
            return false;
        }
        total -= size;
    }
    if (total > max_bytes_) {
        append_ready_ = false;
        return false;
    }
    return true;
}
bool HistoryStore::visit(const std::function<bool(const Bucket &)> &visitor) const {
    if (!safe_history_directory(directory_))
        return false;
    struct stat initial_directory {};
    if (stat(directory_.c_str(), &initial_directory) != 0)
        return false;
    auto *dir = opendir(directory_.c_str());
    if (!dir)
        return false;
    closedir(dir);
    // A callback in a later segment can change a previously exported segment
    // after its individual post-visit check. Retain bounded per-segment
    // identities and fingerprints until the entire export has finished.
    struct VerifiedSegment {
        unsigned number;
        std::size_t size;
        uint64_t fingerprint;
#ifndef ESP_PLATFORM
        uint64_t device = 0;
        uint64_t inode = 0;
#endif
    };
    std::vector<VerifiedSegment> verified;
    const auto unchanged_visited = [&]() {
        if (!same_history_directory(directory_, initial_directory))
            return false;
        for (const auto &segment : verified) {
            const auto segment_path = path(segment.number);
            if (!unchanged_segment_contents(segment_path, segment.size, segment.fingerprint))
                return false;
#ifndef ESP_PLATFORM
            struct stat current {};
            if (lstat(segment_path.c_str(), &current) != 0 || !S_ISREG(current.st_mode) ||
                static_cast<uint64_t>(current.st_dev) != segment.device ||
                static_cast<uint64_t>(current.st_ino) != segment.inode)
                return false;
#endif
        }
        return true;
    };
    for (auto number : segments()) {
        // The directory can be replaced while a visitor processes a prior
        // segment. Recheck before following the next segment pathname.
        if (!same_history_directory(directory_, initial_directory) ||
            !safe_segment_path(path(number), false))
            return false;
        struct stat initial_segment {};
#ifdef ESP_PLATFORM
        const int segment_status = stat(path(number).c_str(), &initial_segment);
#else
        const int segment_status = lstat(path(number).c_str(), &initial_segment);
#endif
        if (segment_status != 0 || !S_ISREG(initial_segment.st_mode) || initial_segment.st_size < 0)
            return false;
        const auto initial_size = static_cast<std::size_t>(initial_segment.st_size);
        uint64_t initial_fingerprint = 0;
        if (!segment_fingerprint(path(number), initial_size, initial_fingerprint) ||
            !segment_has_size(path(number), initial_size))
            return false;
        VerifiedSegment snapshot{number, initial_size, initial_fingerprint};
#ifndef ESP_PLATFORM
        snapshot.device = static_cast<uint64_t>(initial_segment.st_dev);
        snapshot.inode = static_cast<uint64_t>(initial_segment.st_ino);
#endif
        verified.push_back(snapshot);
        std::ifstream input(path(number), std::ios::binary);
        if (!input)
            return false;
        std::size_t good = 0;
        while (auto bucket = next(input)) {
            good = static_cast<std::size_t>(input.tellg());
            if (!visitor(*bucket))
                return same_history_segment(path(number), initial_segment) && unchanged_visited();
            if (!same_history_directory(directory_, initial_directory) ||
                !same_history_segment(path(number), initial_segment))
                return false;
        }
        // A callback may replace the parent directory or the active file even
        // on the final record. Never report a successful mixed-file export.
        if (!same_history_directory(directory_, initial_directory) ||
            !same_history_segment(path(number), initial_segment) ||
            !unchanged_segment_contents(path(number), initial_size, initial_fingerprint))
            return false;
        if (input.bad() || good != file_size(path(number)))
            return false;
    }
    return unchanged_visited();
}
} // namespace monitor
