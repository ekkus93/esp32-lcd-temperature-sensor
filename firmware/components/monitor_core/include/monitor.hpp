#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace monitor {
constexpr std::size_t max_sensors = 5;
constexpr int64_t sample_ms = 5000;
constexpr int64_t bucket_ms = 300000;
constexpr unsigned schema_version = 1;
enum class State {
    normal,
    high,
    low,
    unassigned,
    disabled,
    disconnected,
    stale,
    invalid,
    bus_fault
};
const char *state_name(State state);
uint8_t crc8(const uint8_t *data, std::size_t length);
bool valid_rom(uint64_t rom);
std::string rom_hex(uint64_t rom);
std::optional<double> decode_temperature(const std::array<uint8_t, 9> &scratchpad,
                                         bool conversion_complete);
struct Settings {
    std::string name;
    std::string short_name;
    bool enabled = true;
    bool disconnect_alert = true;
    unsigned order = 0;
    double calibration = 0;
    std::optional<double> low;
    std::optional<double> high;
    double hysteresis = 0.5;
    int64_t persistence_ms = 15000;
};
bool valid_settings(const Settings &settings);
struct Sensor {
    uint64_t rom = 0;
    uint64_t config_revision = 0;
    Settings settings;
    State state = State::unassigned;
    std::optional<double> temperature;
    std::optional<int64_t> last_seen_ms;
    unsigned failures = 0;
    State pending = State::normal;
    std::optional<int64_t> pending_since;
};
struct Reading {
    uint64_t rom;
    std::optional<double> celsius;
    bool conversion_complete = true;
};
struct Event {
    uint64_t sequence;
    uint64_t rom;
    State previous;
    State state;
    int64_t uptime_ms;
};
class Registry {
  public:
    bool discover(uint64_t rom);
    bool configure(uint64_t rom, const Settings &settings,
                   std::optional<int64_t> now_ms = std::nullopt);
    void sample(const std::vector<Reading> &readings, int64_t now_ms, bool bus_fault = false);
    void expire(int64_t now_ms);
    const std::vector<Sensor> &sensors() const;
    const std::vector<Event> &events() const;
    uint64_t sequence() const;
    unsigned excess_count() const;
    bool bus_fault() const;

  private:
    Sensor *find(uint64_t rom);
    void transition(Sensor &sensor, State state, int64_t now_ms);
    void event(uint64_t rom, State previous, State state, int64_t now_ms);
    std::vector<Sensor> sensors_;
    std::vector<Event> events_;
    uint64_t sequence_ = 0;
    uint64_t event_sequence_ = 0;
    unsigned excess_ = 0;
    bool bus_fault_ = false;
};
struct Aggregate {
    uint64_t rom;
    double average;
    double minimum;
    double maximum;
    unsigned count;
    bool partial;
};
struct Bucket {
    uint64_t boot_id;
    int64_t start_uptime_ms;
    std::optional<int64_t> utc_seconds;
    std::vector<Aggregate> sensors;
};
class Aggregator {
  public:
    explicit Aggregator(uint64_t boot_id);
    // Returns only a completed window; callers persist this, never individual readings.
    std::optional<Bucket> add(const Registry &registry, int64_t now_ms,
                              std::optional<int64_t> utc_seconds = std::nullopt);

  private:
    struct Accumulator {
        uint64_t rom;
        double sum = 0;
        double minimum = 0;
        double maximum = 0;
        unsigned count = 0;
    };
    uint64_t boot_id_;
    std::optional<int64_t> start_;
    std::optional<int64_t> utc_;
    std::optional<int64_t> anchor_utc_;
    int64_t anchor_ms_ = 0;
    int64_t deadline_ = 0;
    std::optional<int64_t> last_sample_;
    bool partial_ = false;
    std::vector<Accumulator> accumulators_;
};
class Clock {
  public:
    void synchronize(int64_t monotonic_ms, int64_t utc_seconds);
    std::optional<int64_t> utc(int64_t monotonic_ms) const;

  private:
    std::optional<int64_t> origin_;
    int64_t utc_ = 0;
};
class BootButton {
  public:
    // Start unarmed: BOOT held across reset must first be released.
    bool update(bool pressed, int64_t now_ms);
    unsigned progress(int64_t now_ms) const;

  private:
    bool armed_ = false;
    bool raw_ = false;
    bool stable_ = false;
    bool fired_ = false;
    int64_t changed_ = 0;
    int64_t pressed_ = 0;
};
class DisplayCycle {
  public:
    bool configure(bool extended, unsigned overview_s, unsigned trends_s, int64_t now_ms);
    bool trends(int64_t now_ms) const;

  private:
    bool extended_ = false;
    unsigned overview_s_ = 20;
    unsigned trends_s_ = 10;
    int64_t start_ = 0;
};
} // namespace monitor
