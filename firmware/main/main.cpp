#include "auth_service.hpp"
#include "board_pins.h"
#include "captive_service.hpp"
#include "clock_service.hpp"
#include "device_settings_service.hpp"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "fake_bus.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "history_service.hpp"
#include "http_api.hpp"
#include "monitor.hpp"
#include "nvs_flash.h"
#include "onewire_service.hpp"
#include "provisioning_service.hpp"
#include "settings_service.hpp"
#include "wifi_service.hpp"

int run_core_tests();
extern "C" void app_main() {
    const char *tag = "monitor";
#ifdef CONFIG_MONITOR_SIMULATION
    constexpr bool simulated = true;
#else
    constexpr bool simulated = false;
#endif
    run_core_tests();
    ESP_LOGI(tag, "SELFTEST PASS core behavior");
    esp_err_t nvs = nvs_flash_init();
    // Preserve existing NVS on corruption/schema mismatch; report and keep running.
    if (nvs != ESP_OK)
        ESP_LOGE(tag, "NVS unavailable: %s; no automatic erase", esp_err_to_name(nvs));
    monitor::DeviceSettingsService device_settings;
    auto device_load = device_settings.initialize(simulated);
    if (device_load != ESP_OK)
        ESP_LOGE(tag, "Device settings unavailable: %s; original data preserved",
                 esp_err_to_name(device_load));
    monitor::DeviceOptions device_options;
    uint64_t device_revision = 0;
    bool device_writable = false;
    device_settings.get(device_options, device_revision, device_writable);
    ESP_LOGI(tag, "version=%s idf=%s psram=%u internal_heap=%u", esp_app_get_description()->version,
             esp_get_idf_version(), static_cast<unsigned>(esp_psram_get_size()),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    monitor::HistoryService history;
    if (!history.start(simulated))
        ESP_LOGE(tag, "History worker unavailable");
    monitor::WifiService wifi;
    auto network_status = wifi.start();
    monitor::AuthService auth;
    monitor::HttpApi api;
    uint64_t boot_id = (uint64_t(esp_random()) << 32) | esp_random();
    if (network_status == ESP_OK) {
        if (!monitor::start_clock(device_options))
            ESP_LOGW(tag, "Clock synchronization unavailable");
        auto auth_status = auth.initialize();
        if (auth_status == ESP_OK) {
            if (!api.start(auth, wifi, history, device_settings, boot_id, simulated))
                ESP_LOGE(tag, "HTTP server unavailable");
            if (!monitor::start_captive_dns(wifi))
                ESP_LOGE(tag, "Captive DNS unavailable; direct AP address remains available");
            if (!monitor::start_provisioning_controls(wifi))
                ESP_LOGE(tag, "Provisioning controls unavailable");
        } else
            ESP_LOGE(tag, "Authentication unavailable: %s; HTTP remains closed",
                     esp_err_to_name(auth_status));
    } else
        ESP_LOGE(tag, "Wi-Fi init failed: %s", esp_err_to_name(network_status));
    monitor::Aggregator aggregator(boot_id);
    monitor::Registry registry;
    monitor::SettingsService settings(simulated ? "monitor_sim" : "temp_monitor");
    auto load = settings.load(registry);
    if (load != ESP_OK && load != ESP_ERR_NVS_NOT_FOUND)
        ESP_LOGE(tag, "Sensor settings unavailable: %s", esp_err_to_name(load));
    monitor::OneWireService bus;
    esp_err_t bus_status = simulated ? ESP_OK : bus.initialize();
    if (bus_status != ESP_OK)
        ESP_LOGE(tag, "Bus init failed: %s", esp_err_to_name(bus_status));
    ESP_LOGI(tag, "READY no peripherals required; DQ=%d; LCD/backlight disabled", PIN_ONEWIRE_DATA);
    bool settings_dirty = false;
    uint64_t simulated_tick = 0;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        auto now = esp_timer_get_time() / 1000;
        uint64_t changed_rom = 0;
        monitor::Settings changed_settings;
        for (unsigned update = 0;
             update < 4 && api.next_sensor_update(changed_rom, changed_settings); ++update) {
            if (registry.configure(changed_rom, changed_settings,
                                   simulated ? int64_t(simulated_tick * 5000) : now))
                settings_dirty = true;
        }
        auto before = registry.sensors().size();
        auto readings = simulated ? monitor::fake_readings() : bus.read(now);
        auto sampled_at = simulated ? int64_t(simulated_tick * 5000) : esp_timer_get_time() / 1000;
        registry.sample(readings, sampled_at, bus.fault());
        if (auto bucket = aggregator.add(registry, sampled_at,
                                         simulated ? std::nullopt : monitor::trusted_utc())) {
            if (!history.submit(*bucket))
                ESP_LOGE(tag, "History queue unavailable/full");
        }
        settings_dirty = settings_dirty || registry.sensors().size() != before;
        if (load == ESP_ERR_NVS_NOT_FOUND || load == ESP_OK) {
            if (settings_dirty) {
                auto saved = settings.save(registry);
                if (saved != ESP_OK)
                    ESP_LOGE(tag, "Settings save failed: %s", esp_err_to_name(saved));
                else
                    settings_dirty = false;
            }
        }
        api.publish(registry, sampled_at, history.healthy(),
                    (!simulated && monitor::clock_synchronized()),
                    simulated ? std::string{} : monitor::utc_text());
        if (!simulated || simulated_tick % 20 == 0)
            ESP_LOGI(tag, "HEALTH uptime_ms=%lld sensors=%u bus=%s clock=%s history=%d heap=%u",
                     static_cast<long long>(now), static_cast<unsigned>(registry.sensors().size()),
                     simulated ? "SIMULATED"
                               : (bus.enabled() ? (bus.fault() ? "fault" : "ready") : "disabled"),
                     simulated
                         ? "simulated"
                         : (monitor::clock_synchronized() ? "synchronized" : "unsynchronized"),
                     int(history.healthy()), static_cast<unsigned>(esp_get_free_heap_size()));
        ++simulated_tick;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(simulated ? 250 : 5000));
    }
}
