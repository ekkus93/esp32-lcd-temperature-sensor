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
// A calibration hold (spec 17.3) lasts at most this long, so an abandoned browser cannot pause a
// probe's alarms for longer.
constexpr int64_t max_calibration_hold_ms = 20 * 60000;
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
// The largest calibration offset, either way. It is what a probe's own memory can hold (below); a
// real DS18B20 is within 2 degrees C, so a larger correction would mean a broken probe.
constexpr double calibration_limit_c = 12.7;
// A DS18B20 keeps two user bytes, TH and TL, in its own EEPROM. They hold a copy of the probe's
// calibration offset, so the offset stays with the probe when it moves to another monitor or the
// monitor is factory reset. TH is the offset in 0.1 degree C steps (-12.7 to +12.7); TL is TH XOR
// a fixed pattern, which tells our bytes from factory values. TH -128 means no offset is stored;
// it is written only for a value outside the limit, which validation already rejects.
struct ProbeMemory {
    uint8_t th = 0;
    uint8_t tl = 0;
    bool operator==(const ProbeMemory &other) const {
        return th == other.th && tl == other.tl;
    }
    bool operator!=(const ProbeMemory &other) const {
        return !(*this == other);
    }
};
ProbeMemory encode_probe_calibration(double calibration);
// The offset a probe holds; empty when the bytes are not ours or say that no offset is stored.
std::optional<double> decode_probe_calibration(ProbeMemory memory);
// The TH and TL bytes of a scratchpad read whose CRC is correct.
std::optional<ProbeMemory> probe_memory(const std::array<uint8_t, 9> &scratchpad);
// Whether the probe's own memory holds the monitor's calibration offset.
enum class ProbeCalibration {
    unknown,  // no valid read of the probe's memory yet
    pending,  // the probe holds something else; a write is due
    stored,   // the probe holds the monitor's offset
    restored, // the monitor took this offset from the probe when the probe was new to it
    failed    // writes did not stick; not retried until the offset changes or a restart
};
const char *probe_calibration_name(ProbeCalibration state);
constexpr unsigned max_probe_write_attempts = 3;
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
    // The state a sensor was in when a bus fault began, restored when the fault clears so an
    // alarm episode (and its persistence and hysteresis) is not split by the fault.
    std::optional<State> state_before_fault;
    // When `state` last changed (uptime ms). A bus fault does not change it, so a condition keeps
    // its start time across the fault.
    std::optional<int64_t> state_since_ms;
    // An unconfirmed reading of exactly 85.0 (the DS18B20 power-on value) was just discarded; the
    // next reading decides whether it was real.
    bool unconfirmed_power_on_value = false;
    // What the probe's TH and TL bytes held at the last valid read; empty until one.
    std::optional<ProbeMemory> probe_memory;
    ProbeCalibration probe_calibration = ProbeCalibration::unknown;
    unsigned probe_write_failures = 0;
    // Set for a probe new to this monitor: its first valid read takes the calibration offset its
    // own memory holds. Configuring the sensor first clears it, so the owner's choice wins.
    bool adopt_probe_calibration = false;
    // An ice-bath calibration hold: until this uptime the probe raises no high or low alarm and
    // its readings are not recorded. Kept in RAM only, so it never survives a restart.
    std::optional<int64_t> calibration_hold_until_ms;
};
// Whether the sensor's calibration hold is in force at `now_ms`.
bool calibration_held(const Sensor &sensor, int64_t now_ms);
struct Reading {
    uint64_t rom;
    std::optional<double> celsius;
    bool conversion_complete = true;
    std::optional<ProbeMemory> memory = std::nullopt; // TH and TL, when the CRC was valid
};
struct ProbeScanStatus {
    unsigned discovered_count = 0;
    bool overflow = false;
    bool complete = false;
    bool enabled = false;
    std::vector<uint64_t> discovered_roms;
};
// What a registry event records: a sensor's state change, or the start or end of a hold.
enum class EventType : uint8_t { state, calibration_started, calibration_ended };
// Why an ice-bath hold ended (spec 17.3); the journal stores it as the event's detail.
enum class CalibrationEnd : uint32_t { expired = 1, cancelled = 2, disconnected = 3 };
struct Event {
    uint64_t sequence;
    uint64_t rom;
    State previous;
    State state;
    int64_t uptime_ms;
    EventType type = EventType::state;
    // calibration_started: the hold's length in minutes. calibration_ended: a CalibrationEnd.
    uint32_t detail = 0;
};
} // namespace monitor
