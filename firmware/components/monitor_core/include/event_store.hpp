#pragma once
#include "monitor.hpp"
#include <functional>

namespace monitor {
enum class EventKind : uint8_t { sensor_state, wifi_state, clock_sync, reboot, storage_error };
struct JournalEvent {
    uint64_t sequence = 0;
    uint64_t boot_id = 0;
    int64_t uptime_ms = 0;
    std::optional<int64_t> utc_seconds;
    uint64_t rom = 0;
    EventKind kind = EventKind::sensor_state;
    State previous = State::normal;
    State state = State::normal;
    uint32_t detail = 0;
};
struct JournalPage {
    std::vector<JournalEvent> events;
    uint64_t next_after = 0;
    uint64_t oldest_available = 0;
    bool has_more = false;
    bool available = false;
};
constexpr size_t event_record_bytes = 60;
constexpr size_t event_log_max_bytes = 128 * 1024;

std::vector<uint8_t> encode_event(const JournalEvent &event);
std::optional<JournalEvent> decode_event(const std::vector<uint8_t> &bytes);

class EventStore {
  public:
    explicit EventStore(std::string path, size_t max_bytes = event_log_max_bytes);
    bool recover();
    bool append(const JournalEvent &event);
    JournalPage page(uint64_t after, size_t limit) const;
    size_t bytes() const;

  private:
    bool compact();
    std::string path_;
    size_t max_bytes_;
    uint64_t last_sequence_ = 0;
    std::optional<JournalEvent> last_event_;
};
} // namespace monitor
