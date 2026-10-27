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
bool bus_fault_detected(bool line_low, bool operation_failed, std::size_t expected_devices,
                        std::size_t completed_transactions);
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
} // namespace monitor
