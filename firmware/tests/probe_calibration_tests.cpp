// The calibration copy kept in a probe's own TH/TL bytes: its encoding, when a new probe's copy is
// adopted, and when the monitor rewrites a probe.
#include "sensor_registry.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

namespace {
uint64_t probe_rom(uint8_t serial) {
    uint8_t bytes[7] = {0x28, serial};
    return 0x28 | uint64_t(serial) << 8 | uint64_t(crc8(bytes, 7)) << 56;
}
Reading reading(uint64_t rom, double celsius, std::optional<ProbeMemory> memory) {
    return {rom, celsius, true, memory};
}
const ProbeMemory factory{0x4b, 0x46}; // the TH/TL a genuine DS18B20 ships with
const Sensor &sensor(const Registry &registry, uint64_t rom) {
    for (const auto &item : registry.sensors())
        if (item.rom == rom)
            return item;
    std::cerr << "sensor missing\n";
    std::exit(1);
}
Settings with_calibration(const Sensor &current, double calibration) {
    auto settings = current.settings;
    settings.calibration = calibration;
    return settings;
}

void encoding_round_trips_in_tenths() {
    for (double offset : {0.0, 1.5, -2.3, 12.7, -12.7, 0.1, -0.1})
        CHECK(decode_probe_calibration(encode_probe_calibration(offset)) == offset);
    CHECK(decode_probe_calibration(encode_probe_calibration(0.25)) == 0.3);
    CHECK(decode_probe_calibration(encode_probe_calibration(-0.04)) == 0.0);
    // Too large, or not a number: the probe is told it holds no offset.
    for (double offset : {12.8, -12.8, 20.0, std::nan("")}) {
        const auto none = encode_probe_calibration(offset);
        CHECK(none.th == 0x80 && none.tl == (0x80 ^ 0xa5));
        CHECK(!decode_probe_calibration(none));
    }
}
void factory_and_damaged_bytes_are_not_ours() {
    CHECK(!decode_probe_calibration(factory));
    CHECK(!decode_probe_calibration({0, 0}));
    CHECK(!decode_probe_calibration({0xff, 0xff}));
    auto damaged = encode_probe_calibration(1.5);
    damaged.tl ^= 1;
    CHECK(!decode_probe_calibration(damaged));
}
void memory_needs_a_valid_scratchpad_crc() {
    std::array<uint8_t, 9> scratchpad{0x90, 0x01, 0x0f, 0xaa, 0x7f, 0xff, 0x0c, 0x10, 0};
    scratchpad[8] = crc8(scratchpad.data(), 8);
    const auto memory = probe_memory(scratchpad);
    CHECK(memory && memory->th == 0x0f && memory->tl == 0xaa);
    scratchpad[8] ^= 1;
    CHECK(!probe_memory(scratchpad));
}
void new_probe_adopts_its_stored_offset_before_its_first_reading() {
    Registry registry;
    const auto rom = probe_rom(1);
    CHECK(registry.sample({reading(rom, 20, encode_probe_calibration(-1.5))}, 1000));
    const auto &probe = sensor(registry, rom);
    CHECK(probe.settings.calibration == -1.5);
    CHECK(probe.temperature && *probe.temperature == 18.5);
    CHECK(probe.probe_calibration == ProbeCalibration::restored);
    CHECK(!registry.next_probe_write());
    // Later reads change nothing more.
    CHECK(!registry.sample({reading(rom, 20, encode_probe_calibration(-1.5))}, 6000));
    CHECK(sensor(registry, rom).probe_calibration == ProbeCalibration::restored);
}
void new_probe_with_factory_bytes_is_written_once() {
    Registry registry;
    const auto rom = probe_rom(2);
    CHECK(!registry.sample({reading(rom, 20, factory)}, 1000));
    CHECK(sensor(registry, rom).settings.calibration == 0);
    CHECK(sensor(registry, rom).probe_calibration == ProbeCalibration::pending);
    const auto write = registry.next_probe_write();
    CHECK(write && write->rom == rom && write->memory == encode_probe_calibration(0));
    registry.probe_written(rom, write->memory);
    CHECK(sensor(registry, rom).probe_calibration == ProbeCalibration::stored);
    CHECK(!registry.next_probe_write());
    registry.sample({reading(rom, 20, write->memory)}, 6000);
    CHECK(!registry.next_probe_write());
}
void adoption_waits_for_a_valid_read() {
    Registry registry;
    const auto rom = probe_rom(3);
    registry.sample({reading(rom, 20, std::nullopt)}, 1000); // CRC failed
    CHECK(sensor(registry, rom).probe_calibration == ProbeCalibration::unknown);
    CHECK(!registry.next_probe_write());
    CHECK(registry.sample({reading(rom, 20, encode_probe_calibration(2))}, 6000));
    CHECK(sensor(registry, rom).settings.calibration == 2);
}
void owner_settings_win_over_the_probe() {
    // A probe the monitor already knows (loaded from saved settings) keeps the saved offset.
    Registry registry;
    const auto rom = probe_rom(4);
    CHECK(registry.discover(rom));
    CHECK(registry.configure(rom, with_calibration(sensor(registry, rom), 0.5)));
    CHECK(!registry.sample({reading(rom, 20, encode_probe_calibration(3))}, 1000));
    CHECK(sensor(registry, rom).settings.calibration == 0.5);
    const auto write = registry.next_probe_write();
    CHECK(write && write->memory == encode_probe_calibration(0.5));

    // A new probe configured before its first valid read is not overridden either.
    const auto other = probe_rom(5);
    registry.sample({reading(other, 20, std::nullopt)}, 6000);
    CHECK(registry.configure(other, with_calibration(sensor(registry, other), -0.7)));
    CHECK(!registry.sample({reading(other, 20, encode_probe_calibration(4))}, 11000));
    CHECK(sensor(registry, other).settings.calibration == -0.7);
}
void failed_writes_stop_until_the_offset_changes() {
    Registry registry;
    const auto rom = probe_rom(6);
    registry.sample({reading(rom, 20, factory)}, 1000);
    for (unsigned attempt = 0; attempt < max_probe_write_attempts; ++attempt) {
        CHECK(registry.next_probe_write());
        // A clone that accepts the write but keeps its old EEPROM, then a bus failure.
        registry.probe_written(rom, attempt == 0 ? std::optional(factory) : std::nullopt);
    }
    CHECK(sensor(registry, rom).probe_calibration == ProbeCalibration::failed);
    CHECK(!registry.next_probe_write());
    registry.sample({reading(rom, 20, factory)}, 6000);
    CHECK(!registry.next_probe_write());
    // A new offset is worth another try.
    CHECK(registry.configure(rom, with_calibration(sensor(registry, rom), 1)));
    CHECK(sensor(registry, rom).probe_calibration == ProbeCalibration::pending);
    CHECK(registry.next_probe_write());
}
void large_offset_clears_the_probe_copy() {
    Registry registry;
    const auto rom = probe_rom(7);
    registry.sample({reading(rom, 20, encode_probe_calibration(1))}, 1000);
    CHECK(sensor(registry, rom).probe_calibration == ProbeCalibration::restored);
    CHECK(registry.configure(rom, with_calibration(sensor(registry, rom), 15)));
    const auto write = registry.next_probe_write();
    CHECK(write && !decode_probe_calibration(write->memory));
    registry.probe_written(rom, write->memory);
    CHECK(sensor(registry, rom).probe_calibration == ProbeCalibration::out_of_range);
    // Back in range: rewritten, and now "stored" rather than "restored".
    CHECK(registry.configure(rom, with_calibration(sensor(registry, rom), 1)));
    registry.probe_written(rom, encode_probe_calibration(1));
    CHECK(sensor(registry, rom).probe_calibration == ProbeCalibration::stored);
}
} // namespace

int main() {
    encoding_round_trips_in_tenths();
    factory_and_damaged_bytes_are_not_ours();
    memory_needs_a_valid_scratchpad_crc();
    new_probe_adopts_its_stored_offset_before_its_first_reading();
    new_probe_with_factory_bytes_is_written_once();
    adoption_waits_for_a_valid_read();
    owner_settings_win_over_the_probe();
    failed_writes_stop_until_the_offset_changes();
    large_offset_clears_the_probe_copy();
    std::cout << "probe calibration tests passed\n";
}
