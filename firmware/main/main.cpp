#include "auth_service.hpp"
#include "board_pins.h"
#include "captive_service.hpp"
#include "clock_service.hpp"
#include "device_settings_service.hpp"
#include "display_model.hpp"
#include "display_runtime.hpp"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_psram.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "history_service.hpp"
#include "http_api.hpp"
#include "monitor.hpp"
#include "nvs_flash.h"
#include "ota_boot_policy.hpp"
#include "ota_service.hpp"
#include "provisioning_service.hpp"
#include "sampling_service.hpp"
#include "sensor_diagnostics.hpp"
#include "settings_service.hpp"
#include "wifi_service.hpp"
#include <algorithm>
#include <vector>

namespace {
struct DisplayFrame {
    monitor::Registry registry;
    monitor::TrendCache trend_cache;
    monitor::DeviceOptions options;
    monitor::DisplayStatus status;
    monitor::ProbeScanStatus scan_status;
    int64_t uptime_ms = 0;
    int64_t published_at_ms = 0;
    bool provisioning = false;
    bool logging = false, synchronized = false;
    std::string utc;
    std::string ssid, wifi_key, owner_code;
};

struct DisplayTaskContext {
    monitor::DisplayRuntime *display;
    monitor::HttpApi *api;
    SemaphoreHandle_t mutex;
    bool api_publish_pending = false;
    DisplayFrame frame;
};

void display_refresh_task(void *argument) {
    auto &context = *static_cast<DisplayTaskContext *>(argument);
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        if (xSemaphoreTake(context.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            context.frame.status.button_progress = monitor::boot_hold_progress();
            const auto now_ms = esp_timer_get_time() / 1000;
            const auto uptime_ms = context.frame.uptime_ms +
                                   std::max<int64_t>(0, now_ms - context.frame.published_at_ms);
            context.api_publish_pending |=
                monitor::expire_display_snapshot(context.frame.registry, uptime_ms);
            if (context.api_publish_pending &&
                context.api->publish(context.frame.registry, context.frame.scan_status, uptime_ms,
                                     context.frame.logging, context.frame.synchronized,
                                     context.frame.utc))
                context.api_publish_pending = false;
            context.display->update(context.frame.registry, context.frame.trend_cache,
                                    context.frame.options, context.frame.status, uptime_ms,
                                    context.frame.provisioning, context.frame.ssid,
                                    context.frame.wifi_key, context.frame.owner_code);
            xSemaphoreGive(context.mutex);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(100));
    }
}
} // namespace

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
    else {
        const auto reset = monitor::apply_pending_factory_reset();
        if (reset != ESP_OK)
            ESP_LOGE(tag, "Factory reset could not be completed: %s; reset intent preserved",
                     esp_err_to_name(reset));
    }
    monitor::Registry registry;
    monitor::SettingsService settings(simulated ? "monitor_sim" : "temp_monitor");
    auto load = settings.load(registry);
    bool sensor_settings_writable = settings.writable();
    if (!sensor_settings_writable)
        ESP_LOGE(tag, "Sensor settings unavailable: %s; original data preserved",
                 esp_err_to_name(load));
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
    const bool history_started = history.start(simulated, boot_id);
    if (!history_started)
        ESP_LOGE(tag, "History worker unavailable");
    monitor::WifiService wifi;
    bool prior_clock_synced = false;
    auto network_status = wifi.start();
    monitor::AuthService auth;
    monitor::HttpApi api;
    bool auth_started = false;
    bool http_started = false;
    if (network_status == ESP_OK) {
        if (!monitor::start_clock(device_options))
            ESP_LOGW(tag, "Clock synchronization unavailable");
        auto auth_status = auth.initialize();
        auth_started = auth_status == ESP_OK;
        if (auth_started) {
            http_started = api.start(auth, wifi, history, device_settings, boot_id, simulated,
                                     sensor_settings_writable);
            if (!http_started)
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
    monitor::DisplayRuntime display;
    if (!display.initialize())
        ESP_LOGE(tag, "LCD unavailable; sensor/network services continue without display");
    DisplayTaskContext display_context{&display, &api, xSemaphoreCreateMutex(), false, {}};
    const bool display_task_started = display.ready() && display_context.mutex &&
                                      xTaskCreate(display_refresh_task, "display_refresh", 6144,
                                                  &display_context, 3, nullptr) == pdPASS;
    if (display.ready() && !display_task_started)
        ESP_LOGE(tag, "Independent display refresh unavailable; using acquisition cadence");
    monitor::TrendCache trend_cache;
    monitor::SamplingService sampling;
    monitor::SensorDiagnostics sensor_diagnostics;
    esp_err_t bus_status = sampling.initialize(simulated);
    if (bus_status != ESP_OK)
        ESP_LOGE(tag, "Bus init failed: %s", esp_err_to_name(bus_status));
    ESP_LOGI(tag, "READY no peripherals required; DQ=%d; backlight disabled", PIN_ONEWIRE_DATA);
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
        auto committed_registry = registry;
        bool configuration_update = false;
        bool retire_update = false;
        for (unsigned update = 0;
             update < 4 && api.next_sensor_update(changed_rom, changed_settings, retire_update);
             ++update) {
            const bool changed =
                retire_update
                    ? registry.retire(changed_rom)
                    : registry.configure(changed_rom, changed_settings,
                                         simulated ? int64_t(simulated_tick * 5000) : now);
            if (changed) {
                settings_dirty = true;
                configuration_update = true;
            }
        }
        if (configuration_update) {
            const auto saved = settings.save_or_restore(registry, committed_registry);
            if (saved != ESP_OK) {
                settings_dirty = false;
                sensor_settings_writable = false;
                api.set_sensor_settings_writable(false);
                ESP_LOGE(tag, "Sensor settings commit failed; edit rejected: %s",
                         esp_err_to_name(saved));
            } else {
                settings_dirty = false;
            }
        }
        auto before = registry.sensors().size();
        std::vector<uint64_t> managed_roms;
        managed_roms.reserve(registry.sensors().size());
        for (const auto &sensor : registry.sensors())
            managed_roms.push_back(sensor.rom);
        const int64_t read_started_ms = esp_timer_get_time() / 1000;
        auto readings = sampling.read(now, managed_roms);
        const int64_t read_ended_ms = esp_timer_get_time() / 1000;
        if (sampling.enabled())
            sensor_diagnostics.observe(readings, sampling.fault(), read_ended_ms - read_started_ms);
        auto sampled_at = simulated ? int64_t(simulated_tick * 5000) : read_ended_ms;
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
            trend_cache.add(*bucket);
            if (!history.submit(*bucket))
                ESP_LOGE(tag, "History queue unavailable/full");
        }
        settings_dirty = settings_dirty || registry.sensors().size() != before;
        if (sensor_settings_writable) {
            if (settings_dirty) {
                auto saved = settings.save(registry);
                if (saved != ESP_OK) {
                    ESP_LOGE(tag, "Settings save failed: %s", esp_err_to_name(saved));
                    sensor_settings_writable = false;
                    api.set_sensor_settings_writable(false);
                } else
                    settings_dirty = false;
            }
        }
        api.publish(registry, sampling.scan_status(), sampled_at, history.healthy(),
                    (!simulated && monitor::clock_synchronized()),
                    simulated ? std::string{} : monitor::utc_text());
        if (display.ready()) {
            monitor::DeviceOptions options;
            device_settings.get(options, device_revision, device_writable);
            const auto network = wifi.info();
            monitor::DisplayStatus display_status;
            display_status.wifi = network.state;
            display_status.local_time = simulated ? "" : monitor::local_time_text();
            display_status.logging = history.healthy();
            display_status.button_progress = monitor::boot_hold_progress();
            display_status.ota_state = monitor::ota_status().state;
            if (display_task_started) {
                if (xSemaphoreTake(display_context.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    display_context.frame.registry = registry;
                    display_context.frame.trend_cache = trend_cache;
                    display_context.frame.options = options;
                    display_context.frame.status = display_status;
                    display_context.frame.scan_status = sampling.scan_status();
                    display_context.frame.uptime_ms = sampled_at;
                    display_context.frame.published_at_ms = esp_timer_get_time() / 1000;
                    display_context.frame.logging = history.healthy();
                    display_context.frame.synchronized =
                        !simulated && monitor::clock_synchronized();
                    display_context.frame.utc = simulated ? std::string{} : monitor::utc_text();
                    display_context.frame.provisioning = network.provisioning;
                    display_context.frame.ssid = wifi.ap_ssid();
                    display_context.frame.wifi_key = wifi.ap_password();
                    display_context.frame.owner_code = auth.setup_secret();
                    xSemaphoreGive(display_context.mutex);
                }
            } else {
                display.update(registry, trend_cache, options, display_status, sampled_at,
                               network.provisioning, wifi.ap_ssid(), wifi.ap_password(),
                               auth.setup_secret());
            }
        }
        if (!simulated && !ota_boot_checked && ++healthy_cycles >= 2) {
            const auto *running = esp_ota_get_running_partition();
            esp_ota_img_states_t state{};
            const auto state_result =
                running ? esp_ota_get_state_partition(running, &state) : ESP_ERR_NOT_FOUND;
            if (state_result != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) {
                ota_boot_checked = true;
            } else {
                monitor::OtaBootHealth health;
                health.completed_cycles = healthy_cycles;
                health.history_worker_started = history_started;
                health.wifi_stack_started = network_status == ESP_OK;
                health.authentication_started = auth_started;
                health.http_server_started = http_started;
                health.sampling_initialized = bus_status == ESP_OK;
                if (!monitor::ota_boot_health_ready(health)) {
                    if (healthy_cycles == 2)
                        ESP_LOGE(tag, "OTA startup services incomplete; image remains pending");
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
        }
        if (!simulated || simulated_tick % 20 == 0) {
            const auto internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            const auto internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
            const auto internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
            const auto psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            const auto psram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
            const auto stack_min = uxTaskGetStackHighWaterMark(nullptr);
            ESP_LOGI(
                tag,
                "HEALTH uptime_ms=%lld sensors=%u bus=%s clock=%s history=%d heap=%u "
                "internal_free=%u internal_min=%u internal_largest=%u psram_free=%u "
                "psram_min=%u main_stack_min=%u",
                static_cast<long long>(now), static_cast<unsigned>(registry.sensors().size()),
                simulated
                    ? "SIMULATED"
                    : (sampling.enabled() ? (sampling.fault() ? "fault" : "ready") : "disabled"),
                simulated ? "simulated"
                          : (monitor::clock_synchronized() ? "synchronized" : "unsynchronized"),
                int(history.healthy()), static_cast<unsigned>(esp_get_free_heap_size()),
                static_cast<unsigned>(internal_free), static_cast<unsigned>(internal_min),
                static_cast<unsigned>(internal_largest), static_cast<unsigned>(psram_free),
                static_cast<unsigned>(psram_min), static_cast<unsigned>(stack_min));
            // Numeric-only physical-bus evidence. Never log ROM IDs,
            // credentials, network names or fabricated simulated readings.
            if (sampling.enabled()) {
                ESP_LOGI(tag,
                         "SENSOR_DIAG cycles=%llu last_probes=%u max_probes=%u "
                         "valid=%llu invalid=%llu fault_cycles=%llu "
                         "last_read_ms=%lld max_read_ms=%lld",
                         static_cast<unsigned long long>(sensor_diagnostics.cycles),
                         static_cast<unsigned>(sensor_diagnostics.last_probes),
                         static_cast<unsigned>(sensor_diagnostics.max_probes),
                         static_cast<unsigned long long>(sensor_diagnostics.valid),
                         static_cast<unsigned long long>(sensor_diagnostics.invalid),
                         static_cast<unsigned long long>(sensor_diagnostics.fault_cycles),
                         static_cast<long long>(sensor_diagnostics.last_read_ms),
                         static_cast<long long>(sensor_diagnostics.max_read_ms));
            }
        }
        ++simulated_tick;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(simulated ? 250 : 5000));
    }
}
