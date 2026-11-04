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
    return onewire_new_bus_rmt(&config, &rmt, &bus_);
#else
    return ESP_OK;
#endif
}
std::vector<Reading> OneWireService::read(int64_t now_ms, const std::vector<uint64_t> &managed) {
    std::vector<Reading> readings;
    if (!bus_)
        return readings;
    const bool line_low = gpio_get_level(static_cast<gpio_num_t>(PIN_ONEWIRE_DATA)) == 0;
    fault_ = bus_fault_detected(line_low, false, 0, 0);
    if (fault_)
        return readings;
    if (now_ms >= next_scan_) {
        next_scan_ = now_ms + 30000;
        onewire_device_iter_handle_t iterator = nullptr;
        if (onewire_new_device_iter(bus_, &iterator) != ESP_OK) {
            scan_status_ = probe_scan_status(0, false, false);
            fault_ = bus_fault_detected(false, true, roms_.size(), 0);
            next_scan_ = now_ms + 5000;
            return readings;
        }
        std::vector<uint64_t> observed;
        onewire_device_t device{};
        esp_err_t result = ESP_OK;
        // Bound enumeration, including unexpected families and excess devices.
        unsigned enumerated = 0;
        for (; enumerated < 16; ++enumerated) {
            result = onewire_device_iter_get_next(iterator, &device);
            if (result != ESP_OK)
                break;
            observed.push_back(device.address);
        }
        onewire_del_device_iter(iterator);
        const bool enumeration_capped = enumerated == 16 && result == ESP_OK;
        const bool iterator_complete = result == ESP_ERR_NOT_FOUND;
        auto discovery = collect_probe_discovery(observed, iterator_complete, enumeration_capped);
        scan_status_ = discovery.status;
        if (scan_status_.complete)
            roms_ = select_probe_reads(discovery.roms, managed);
        else {
            next_scan_ = now_ms + 5000;
            if (!enumeration_capped) {
                fault_ = bus_fault_detected(false, true, roms_.size(), 0);
                return readings;
            }
            auto candidates = discovery.roms;
            candidates.insert(candidates.end(), roms_.begin(), roms_.end());
            roms_ = select_probe_reads(candidates, managed);
        }
    }
    if (roms_.empty())
        return readings;
    if (onewire_bus_reset(bus_) != ESP_OK) {
        fault_ = bus_fault_detected(false, true, roms_.size(), 0);
        next_scan_ = now_ms + 5000;
        return readings;
    }
    const uint8_t convert[] = {0xcc, 0x44}; // SKIP ROM, broadcast Convert T.
    if (onewire_bus_write_bytes(bus_, convert, sizeof(convert)) != ESP_OK) {
        fault_ = bus_fault_detected(false, true, roms_.size(), 0);
        next_scan_ = now_ms + 5000;
        return readings;
    }
    vTaskDelay(pdMS_TO_TICKS(750)); // This dedicated acquisition task yields to Wi-Fi/UI.
    // A released bit after the worst-case 12-bit interval confirms conversion completion.
    uint8_t complete = 0;
    if (onewire_bus_read_bit(bus_, &complete) != ESP_OK || !complete) {
        fault_ = bus_fault_detected(false, true, roms_.size(), 0);
        next_scan_ = now_ms + 5000;
        return readings;
    }
    std::size_t completed_transactions = 0;
    for (auto rom : roms_) {
        std::optional<double> value;
        bool transaction_complete = false;
        if (onewire_bus_reset(bus_) == ESP_OK) {
            std::array<uint8_t, 10> command{};
            command[0] = 0x55; // MATCH ROM.
            for (unsigned i = 0; i < 8; ++i)
                command[i + 1] = (rom >> (8 * i)) & 0xff;
            command[9] = 0xbe; // READ SCRATCHPAD.
            std::array<uint8_t, 9> data{};
            if (onewire_bus_write_bytes(bus_, command.data(), command.size()) == ESP_OK &&
                onewire_bus_read_bytes(bus_, data.data(), data.size()) == ESP_OK) {
                transaction_complete = true;
                value = decode_temperature(data, true);
            }
        }
        completed_transactions += transaction_complete;
        readings.push_back({rom, value, true});
        if (!value)
            next_scan_ = now_ms + 5000;
    }
    // No addressed scratchpad transaction completed: all expected probes failed
    // together, so report one shared-bus fault instead of five sensor failures.
    if (roms_.size() && !completed_transactions)
        next_scan_ = now_ms + 5000;
    fault_ = bus_fault_detected(false, false, roms_.size(), completed_transactions);
    return readings;
}
} // namespace monitor
