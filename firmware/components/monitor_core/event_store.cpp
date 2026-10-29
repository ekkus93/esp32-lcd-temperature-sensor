#include "event_store.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
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
    if (input.gcount() != 0 && input.gcount() != static_cast<std::streamsize>(buffer.size()))
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
    if (max_bytes_ < 2 * event_record_bytes)
        return false;
    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        FILE *created = std::fopen(path_.c_str(), "wb");
        if (!created)
            return false;
        if (std::fclose(created) != 0)
            return false;
        last_sequence_ = 0;
        last_event_.reset();
        append_ready_ = true;
        return true;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    if (bytes.size() > max_bytes_)
        return false;
    const size_t complete = bytes.size() / event_record_bytes;
    uint64_t previous_sequence = 0;
    std::optional<JournalEvent> recovered_last_event;
    for (size_t offset = 0; offset < complete * event_record_bytes; offset += event_record_bytes) {
        std::vector<uint8_t> record(bytes.begin() + offset,
                                    bytes.begin() + offset + event_record_bytes);
        auto event = decode_event(record);
        if (!event || event->sequence <= previous_sequence)
            return false;
        previous_sequence = event->sequence;
        recovered_last_event = *event;
    }
    // Never modify a damaged journal merely because its final write was
    // interrupted: validate every complete record before trimming the tail.
    const size_t tail_start = complete * event_record_bytes;
    if (bytes.size() != tail_start) {
        const size_t remaining = bytes.size() - tail_start;
        // Preserve even a four- or five-byte prefix of a newer TME format.
        // Waiting for all six header bytes used to discard such evidence as
        // an interrupted write from the current firmware.
        if (remaining >= 4 && bytes[tail_start] == 'T' && bytes[tail_start + 1] == 'M' &&
            bytes[tail_start + 2] == 'E') {
            if (bytes[tail_start + 3] != '1')
                return false;
            if (remaining >= 5 && bytes[tail_start + 4] != 1)
                return false;
            if (remaining >= 6 && bytes[tail_start + 5] != 0)
                return false;
        }
        if (truncate(path_.c_str(), static_cast<off_t>(tail_start)) != 0)
            return false;
    }
    last_sequence_ = previous_sequence;
    last_event_ = recovered_last_event;
    append_ready_ = true;
    return true;
}

bool EventStore::compact() {
    auto events = read_all(path_);
    if (events.empty())
        return false;
    const size_t keep = std::max<size_t>(1, events.size() / 2);
    const std::string temporary = path_ + ".tmp";
    FILE *output = std::fopen(temporary.c_str(), "wb");
    if (!output)
        return false;
    bool ok = true;
    for (auto it = events.end() - keep; it != events.end(); ++it) {
        auto bytes = encode_event(*it);
        if (bytes.size() != event_record_bytes ||
            std::fwrite(bytes.data(), 1, bytes.size(), output) != bytes.size()) {
            ok = false;
            break;
        }
    }
    if (std::fflush(output) != 0 || fsync(fileno(output)) != 0)
        ok = false;
    if (std::fclose(output) != 0)
        ok = false;
    if (!ok || std::rename(temporary.c_str(), path_.c_str()) != 0) {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}

bool EventStore::append(const JournalEvent &input) {
    if (!append_ready_ || !valid_event(input))
        return false;
    if (last_event_ && input.boot_id == last_event_->boot_id &&
        input.uptime_ms >= last_event_->uptime_ms &&
        input.uptime_ms - last_event_->uptime_ms < 1000 && input.kind == last_event_->kind &&
        input.rom == last_event_->rom && input.previous == last_event_->previous &&
        input.state == last_event_->state && input.utc_seconds == last_event_->utc_seconds &&
        input.detail == last_event_->detail)
        return true;
    if (bytes() + event_record_bytes > max_bytes_ && !compact())
        return false;
    JournalEvent event = input;
    event.sequence = last_sequence_ + 1;
    auto encoded = encode_event(event);
    if (encoded.empty())
        return false;
    FILE *output = std::fopen(path_.c_str(), "ab");
    if (!output)
        return false;
    bool ok = std::fwrite(encoded.data(), 1, encoded.size(), output) == encoded.size();
    if (std::fflush(output) != 0 || fsync(fileno(output)) != 0)
        ok = false;
    if (std::fclose(output) != 0)
        ok = false;
    if (!ok) {
        append_ready_ = false;
        return false;
    }
    last_sequence_ = event.sequence;
    last_event_ = event;
    return true;
}

JournalPage EventStore::page(uint64_t after, size_t limit) const {
    JournalPage page{};
    if (!limit || limit > 50)
        return page;
    std::ifstream input(path_, std::ios::binary);
    if (!input)
        return page;
    page.available = true;
    uint64_t previous_sequence = 0;
    std::array<uint8_t, event_record_bytes> buffer{};
    while (input.read(reinterpret_cast<char *>(buffer.data()), buffer.size())) {
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
    if (input.gcount() != 0 && input.gcount() != static_cast<std::streamsize>(buffer.size()))
        return {};
    if (page.events.empty())
        page.next_after = after;
    return page;
}

size_t EventStore::bytes() const {
    std::ifstream input(path_, std::ios::binary | std::ios::ate);
    return input ? static_cast<size_t>(input.tellg()) : 0;
}
} // namespace monitor
