#include "auth_service.hpp"
#include "board_pins.h"
#include "captive_service.hpp"
#include "clock_service.hpp"
#include "device_settings_service.hpp"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_psram.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "history_service.hpp"
#include "http_api.hpp"
#include "monitor.hpp"
#include "nvs_flash.h"
#include "provisioning_service.hpp"
#include "sampling_service.hpp"
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
    ESP_LOGI(tag, "reset_reason=%d", static_cast<int>(esp_reset_reason()));
    uint64_t boot_id = (uint64_t(esp_random()) << 32) | esp_random();
    monitor::HistoryService history;
    if (!history.start(simulated, boot_id))
        ESP_LOGE(tag, "History worker unavailable");
    monitor::WifiService wifi;
    bool prior_clock_synced = false;
    auto network_status = wifi.start();
    monitor::AuthService auth;
    monitor::HttpApi api;
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
    monitor::SamplingService sampling;
    esp_err_t bus_status = sampling.initialize(simulated);
    if (bus_status != ESP_OK)
        ESP_LOGE(tag, "Bus init failed: %s", esp_err_to_name(bus_status));
    ESP_LOGI(tag, "READY no peripherals required; DQ=%d; LCD/backlight disabled", PIN_ONEWIRE_DATA);
    monitor::JournalEvent boot_event{};
    boot_event.boot_id = boot_id;
    boot_event.kind = monitor::EventKind::reboot;
    if (!simulated)
        boot_event.utc_seconds = monitor::trusted_utc();
    history.submit_event(boot_event);
    bool settings_dirty = false;
    uint64_t simulated_tick = 0;
    uint64_t last_event_sequence = 0;
    std::string prior_wifi_state = wifi.info().state;
    unsigned healthy_cycles = 0;
    bool ota_boot_checked = false;
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
        auto readings = sampling.read(now);
        auto sampled_at = simulated ? int64_t(simulated_tick * 5000) : esp_timer_get_time() / 1000;
        registry.sample(readings, sampled_at, sampling.fault());
        for (const auto &event : registry.events()) {
            if (event.sequence <= last_event_sequence)
                continue;
            monitor::JournalEvent record{};
            record.boot_id = boot_id;
            record.uptime_ms = event.uptime_ms;
            record.utc_seconds = simulated ? std::nullopt : monitor::trusted_utc();
            record.rom = event.rom;
            record.kind = monitor::EventKind::sensor_state;
            record.previous = event.previous;
            record.state = event.state;
            if (!history.submit_event(record))
                ESP_LOGE(tag, "Event queue full; acquisition continues");
            last_event_sequence = event.sequence;
        }
        const auto network = wifi.info();
        if (network.state != prior_wifi_state) {
            auto wifi_code = [](const std::string &state) -> uint32_t {
                if (state == "connecting")
                    return 1;
                if (state == "connected")
                    return 2;
                if (state == "setup")
                    return 3;
                return 0;
            };
            monitor::JournalEvent event{};
            event.boot_id = boot_id;
            event.uptime_ms = sampled_at;
            event.utc_seconds = simulated ? std::nullopt : monitor::trusted_utc();
            event.kind = monitor::EventKind::wifi_state;
            event.detail = (wifi_code(prior_wifi_state) << 8) | wifi_code(network.state);
            history.submit_event(event);
            prior_wifi_state = network.state;
        }
        const bool clock_synced = !simulated && monitor::clock_synchronized();
        if (clock_synced != prior_clock_synced) {
            monitor::JournalEvent event{};
            event.boot_id = boot_id;
            event.uptime_ms = sampled_at;
            event.utc_seconds = clock_synced ? monitor::trusted_utc() : std::nullopt;
            event.kind = monitor::EventKind::clock_sync;
            event.detail = clock_synced ? 1 : 0;
            history.submit_event(event);
            prior_clock_synced = clock_synced;
        }
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
        if (!simulated && !ota_boot_checked && ++healthy_cycles >= 2) {
            const auto *running = esp_ota_get_running_partition();
            esp_ota_img_states_t state{};
            const auto state_result =
                running ? esp_ota_get_state_partition(running, &state) : ESP_ERR_NOT_FOUND;
            if (state_result != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) {
                ota_boot_checked = true;
            } else {
                const esp_err_t valid = esp_ota_mark_app_valid_cancel_rollback();
                if (valid == ESP_OK) {
                    ESP_LOGI(tag, "OTA startup health passed; rollback cancelled");
                    ota_boot_checked = true;
                } else {
                    ESP_LOGE(tag, "OTA image remains pending verification: %s",
                             esp_err_to_name(valid));
                }
            }
        }
        if (!simulated || simulated_tick % 20 == 0)
            ESP_LOGI(
                tag, "HEALTH uptime_ms=%lld sensors=%u bus=%s clock=%s history=%d heap=%u",
                static_cast<long long>(now), static_cast<unsigned>(registry.sensors().size()),
                simulated
                    ? "SIMULATED"
                    : (sampling.enabled() ? (sampling.fault() ? "fault" : "ready") : "disabled"),
                simulated ? "simulated"
                          : (monitor::clock_synchronized() ? "synchronized" : "unsynchronized"),
                int(history.healthy()), static_cast<unsigned>(esp_get_free_heap_size()));
        ++simulated_tick;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(simulated ? 250 : 5000));
    }
}
