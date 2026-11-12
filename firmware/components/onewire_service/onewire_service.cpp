#include "onewire_service.hpp"
#include "board_pins.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "onewire_device.h"
#include <algorithm>
#include <array>
namespace monitor {
esp_err_t OneWireService::initialize() {
#ifdef CONFIG_MONITOR_SENSOR_BUS
    onewire_bus_config_t config = {};
    config.bus_gpio_num = PIN_ONEWIRE_DATA;
    onewire_bus_rmt_config_t rmt = {};
    rmt.max_rx_bytes = 10;
    const esp_err_t result = onewire_new_bus_rmt(&config, &rmt, &bus_);
    init_failed_ = result != ESP_OK;
    return result;
#else
    return ESP_OK;
#endif
}
std::vector<Reading> OneWireService::read(int64_t now_ms, const std::vector<uint64_t> &managed) {
    std::vector<Reading> readings;
    if (!bus_)
        return readings;
    const bool line_low = gpio_get_level(static_cast<gpio_num_t>(PIN_ONEWIRE_DATA)) == 0;
    if (!policy_.begin_cycle(line_low))
        return readings;
    if (policy_.scan_due(now_ms)) {
        ScanInput scan;
        onewire_device_iter_handle_t iterator = nullptr;
        if (onewire_new_device_iter(bus_, &iterator) != ESP_OK) {
            scan.iterator_created = false;
        } else {
            onewire_device_t device{};
            esp_err_t result = ESP_OK;
            // Bound enumeration, including unexpected families and excess devices.
            unsigned enumerated = 0;
            for (; enumerated < 16; ++enumerated) {
                result = onewire_device_iter_get_next(iterator, &device);
                if (result != ESP_OK)
                    break;
                scan.observed.push_back(device.address);
            }
            onewire_del_device_iter(iterator);
            scan.capped = enumerated == 16 && result == ESP_OK;
            scan.ended_normally = result == ESP_ERR_NOT_FOUND;
            scan.crc_error = result == ESP_ERR_INVALID_CRC;
        }
        if (!policy_.apply_scan(scan, now_ms, managed))
            return readings;
    }
    if (!policy_.has_probes())
        return readings;
    if (onewire_bus_reset(bus_) != ESP_OK) {
        policy_.transaction_failed(now_ms);
        return readings;
    }
    const uint8_t convert[] = {0xcc, 0x44}; // SKIP ROM, broadcast Convert T.
    if (onewire_bus_write_bytes(bus_, convert, sizeof(convert)) != ESP_OK) {
        policy_.transaction_failed(now_ms);
        return readings;
    }
    vTaskDelay(pdMS_TO_TICKS(750)); // This dedicated acquisition task yields to Wi-Fi/UI.
    // A released bit after the worst-case 12-bit interval confirms conversion completion.
    uint8_t complete = 0;
    if (onewire_bus_read_bit(bus_, &complete) != ESP_OK || !complete) {
        policy_.transaction_failed(now_ms);
        return readings;
    }
    std::size_t completed_transactions = 0;
    bool any_value_missing = false;
    for (auto rom : policy_.roms()) {
        std::optional<double> value;
        bool transaction_complete = false;
        std::optional<ProbeMemory> memory;
        if (const auto data = read_scratchpad(rom)) {
            transaction_complete = true;
            value = decode_temperature(*data, true);
            memory = probe_memory(*data);
        }
        completed_transactions += transaction_complete;
        readings.push_back({rom, value, true, memory});
        any_value_missing = any_value_missing || !value;
    }
    policy_.finish_reads(now_ms, completed_transactions, any_value_missing);
    return readings;
}
bool OneWireService::select(uint64_t rom) {
    std::array<uint8_t, 9> command{};
    command[0] = 0x55; // MATCH ROM.
    for (unsigned i = 0; i < 8; ++i)
        command[i + 1] = (rom >> (8 * i)) & 0xff;
    return onewire_bus_reset(bus_) == ESP_OK &&
           onewire_bus_write_bytes(bus_, command.data(), command.size()) == ESP_OK;
}
std::optional<std::array<uint8_t, 9>> OneWireService::read_scratchpad(uint64_t rom) {
    const uint8_t read = 0xbe; // READ SCRATCHPAD.
    std::array<uint8_t, 9> data{};
    if (!select(rom) || onewire_bus_write_bytes(bus_, &read, 1) != ESP_OK ||
        onewire_bus_read_bytes(bus_, data.data(), data.size()) != ESP_OK)
        return std::nullopt;
    return data;
}
std::optional<ProbeMemory> OneWireService::store(uint64_t rom, ProbeMemory memory) {
    if (!bus_)
        return std::nullopt;
    // WRITE SCRATCHPAD: TH, TL and the configuration byte (0x7f is 12-bit resolution, which the
    // 750 ms conversion wait assumes).
    const uint8_t write[] = {0x4e, memory.th, memory.tl, 0x7f};
    if (!select(rom) || onewire_bus_write_bytes(bus_, write, sizeof(write)) != ESP_OK)
        return std::nullopt;
    // Copy to EEPROM only bytes that arrived intact, so a garbled write is never made permanent.
    const auto written = read_scratchpad(rom);
    const auto arrived = written ? probe_memory(*written) : std::nullopt;
    if (arrived && *arrived == memory) {
        const uint8_t copy = 0x48; // COPY SCRATCHPAD.
        if (!select(rom) || onewire_bus_write_bytes(bus_, &copy, 1) != ESP_OK)
            return std::nullopt;
        vTaskDelay(pdMS_TO_TICKS(20)); // The datasheet allows up to 10 ms; the probe is powered.
    }
    // RECALL E2 reloads the scratchpad from EEPROM, so the result (and later reads) show what the
    // probe will keep after a power cut, not an uncopied write. The probe answers 1 when done.
    const uint8_t recall = 0xb8;
    if (!select(rom) || onewire_bus_write_bytes(bus_, &recall, 1) != ESP_OK)
        return std::nullopt;
    uint8_t done = 0;
    for (unsigned poll = 0; poll < 200 && !done; ++poll) // up to about 14 ms
        if (onewire_bus_read_bit(bus_, &done) != ESP_OK)
            return std::nullopt;
    if (!done)
        return std::nullopt;
    const auto kept = read_scratchpad(rom);
    return kept ? probe_memory(*kept) : std::nullopt;
}
} // namespace monitor
