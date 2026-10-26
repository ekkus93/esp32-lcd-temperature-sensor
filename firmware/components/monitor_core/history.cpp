#include "history.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
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
// Read one bounded record. Truncated/corrupt records stop scanning at their offset.
std::optional<Bucket> next(std::istream &in) {
    std::vector<uint8_t> bytes(8);
    if (!in.read(reinterpret_cast<char *>(bytes.data()), 8))
        return std::nullopt;
    auto size = get(bytes, 6, 2);
    if (size < 40 || size > 160)
        return std::nullopt;
    bytes.resize(size);
    if (!in.read(reinterpret_cast<char *>(bytes.data() + 8), size - 8))
        return std::nullopt;
    return decode_bucket(bytes);
}
} // namespace
std::vector<uint8_t> encode_bucket(const Bucket &bucket) {
    if (bucket.sensors.size() > max_sensors || bucket.start_uptime_ms < 0)
        return {};
    std::vector<uint8_t> out{'T', 'M', 'H', '1'};
    put(out, 1, 2);
    put(out, 40 + 24 * bucket.sensors.size(), 2);
    put(out, bucket.boot_id, 8);
    put(out, bucket.start_uptime_ms, 8);
    put(out, bucket.utc_seconds.value_or(0), 8);
    put(out, bucket.sensors.size(), 1);
    put(out, bucket.utc_seconds.has_value(), 1);
    put(out, 0, 2);
    std::vector<uint64_t> identities;
    for (const auto &a : bucket.sensors) {
        if (!valid_rom(a.rom) || !a.count || a.count > 65535 || !std::isfinite(a.average) ||
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
    }
    put(out, crc32(out.data(), out.size()), 4);
    return out;
}
std::optional<Bucket> decode_bucket(const std::vector<uint8_t> &in) {
    if (in.size() < 40 || in.size() > 160 || std::memcmp(in.data(), "TMH1", 4) ||
        get(in, 4, 2) != 1 || get(in, 6, 2) != in.size() || in[32] > 5 ||
        in.size() != 40 + 24 * std::size_t(in[32]) || in[33] > 1 ||
        get(in, in.size() - 4, 4) != crc32(in.data(), in.size() - 4))
        return std::nullopt;
    Bucket bucket{get(in, 8, 8), static_cast<int64_t>(get(in, 16, 8)), std::nullopt, {}};
    if (in[33])
        bucket.utc_seconds = static_cast<int64_t>(get(in, 24, 8));
    for (unsigned i = 0; i < in[32]; ++i) {
        std::size_t pos = 36 + i * 24;
        auto value = [&](std::size_t at) { return static_cast<int32_t>(get(in, at, 4)) / 1000.0; };
        bucket.sensors.push_back({get(in, pos, 8), value(pos + 8), value(pos + 12), value(pos + 16),
                                  unsigned(get(in, pos + 20, 2)), bool(in[pos + 22])});
    }
    // Semantic validation uses the same constraints as the writer.
    if (encode_bucket(bucket).empty())
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
    if (mkdir(directory_.c_str(), 0700) != 0) {
        struct stat info {};
        if (stat(directory_.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
            return false;
    }
    auto files = segments();
    active_ = files.empty() ? 0 : files.back();
    for (auto number : files) {
        std::ifstream input(path(number), std::ios::binary);
        if (!input)
            return false;
        std::size_t good = 0;
        while (auto bucket = next(input))
            good = static_cast<std::size_t>(input.tellg());
        if (good != file_size(path(number))) {
            // Only the active tail may be repaired. Never alter completed segments.
            if (number != active_)
                return false;
            input.clear();
            input.seekg(static_cast<std::streamoff>(good));
            std::array<char, 8> header{};
            input.read(header.data(), header.size());
            if (input.gcount() >= 6 && std::memcmp(header.data(), "TMH", 3) == 0 &&
                (header[3] != '1' || header[4] != 1 || header[5] != 0))
                return false; // Preserve a newer schema during firmware rollback.
            // Refuse to truncate an interior corrupt record if later valid data exists.
            input.clear();
            input.seekg(static_cast<std::streamoff>(good + 1));
            std::vector<uint8_t> tail((std::istreambuf_iterator<char>(input)), {});
            for (std::size_t offset = 0; offset + 40 <= tail.size(); ++offset) {
                if (std::memcmp(tail.data() + offset, "TMH1", 4) != 0)
                    continue;
                auto length = uint16_t(tail[offset + 6]) | (uint16_t(tail[offset + 7]) << 8);
                if (length >= 40 && length <= 160 && offset + length <= tail.size()) {
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
    return true;
}
std::size_t HistoryStore::bytes() const {
    std::size_t total = 0;
    for (auto number : segments())
        total += file_size(path(number));
    return total;
}
bool HistoryStore::append(const Bucket &bucket) {
    auto data = encode_bucket(bucket);
    if (data.empty() || segment_bytes_ < data.size() || max_bytes_ < 2 * segment_bytes_)
        return false;
    if (file_size(path(active_)) + data.size() > segment_bytes_) {
        if (active_ >= 99999999)
            return false;
        ++active_;
    }
    auto files = segments();
    std::size_t total = bytes();
    for (auto number : files) {
        if (total + data.size() <= max_bytes_)
            break;
        if (number == active_)
            return false;
        auto size = file_size(path(number));
        if (std::remove(path(number).c_str()) != 0)
            return false;
        total -= size;
    }
    if (total + data.size() > max_bytes_)
        return false;
    FILE *output = std::fopen(path(active_).c_str(), "ab");
    if (!output)
        return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), output) == data.size();
    if (std::fflush(output) != 0)
        ok = false;
    if (fsync(fileno(output)) != 0)
        ok = false;
    if (std::fclose(output) != 0)
        ok = false;
    return ok;
}
bool HistoryStore::visit(const std::function<bool(const Bucket &)> &visitor) const {
    for (auto number : segments()) {
        std::ifstream input(path(number), std::ios::binary);
        if (!input)
            return false;
        std::size_t good = 0;
        while (auto bucket = next(input)) {
            good = static_cast<std::size_t>(input.tellg());
            if (!visitor(*bucket))
                return true;
        }
        if (good != file_size(path(number)))
            return false;
    }
    return true;
}
} // namespace monitor
