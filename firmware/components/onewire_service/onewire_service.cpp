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
std::vector<Reading> OneWireService::read(int64_t now_ms) {
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
            fault_ = bus_fault_detected(false, true, roms_.size(), 0);
            next_scan_ = now_ms + 5000;
            return readings;
        }
        std::vector<uint64_t> discovered;
        onewire_device_t device{};
        esp_err_t result = ESP_OK;
        // Bound enumeration, including unexpected families and excess devices.
        for (unsigned i = 0; i < 16; ++i) {
            result = onewire_device_iter_get_next(iterator, &device);
            if (result != ESP_OK)
                break;
            if (valid_rom(device.address))
                discovered.push_back(device.address);
        }
        onewire_del_device_iter(iterator);
        std::sort(discovered.begin(), discovered.end());
        discovered.erase(std::unique(discovered.begin(), discovered.end()), discovered.end());
        if (discovered.size() > max_sensors)
            discovered.resize(max_sensors);
        roms_ = discovered;
        if (result != ESP_OK && result != ESP_ERR_NOT_FOUND) {
            fault_ = bus_fault_detected(false, true, roms_.size(), 0);
            next_scan_ = now_ms + 5000;
            return readings;
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
