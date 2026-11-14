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
#include "loop_adapters.hpp"
#include "monitor.hpp"
#include "monitor_loop.hpp"
#include "nvs_flash.h"
#include "ota_boot_policy.hpp"
#include "ota_service.hpp"
#include "provisioning_service.hpp"
#include "sampling_service.hpp"
#include "sensor_diagnostics.hpp"
#include "sensor_settings_persistence.hpp"
#include "settings_service.hpp"
#include "wifi_service.hpp"
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

extern "C" void app_main() {
    const char *tag = "monitor";
#ifdef CONFIG_MONITOR_SIMULATION
    constexpr bool simulated = true;
#else
    constexpr bool simulated = false;
#endif
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
    monitor::SensorSettingsPersistence persistence(settings);
    const bool sensor_settings_writable = settings.writable();
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
    auto network_status = wifi.start(device_options.hostname);
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
        } else
            ESP_LOGE(tag,
                     "Authentication unavailable: %s; HTTP remains closed. Use the USB recovery "
                     "console (RESET_ADMIN_CREDENTIALS CONFIRM) to reset it.",
                     esp_err_to_name(auth_status));
        // The USB recovery console and the BOOT button service do not depend on authentication:
        // they are the way out when the stored credentials are damaged.
        if (!monitor::start_provisioning_controls(wifi))
            ESP_LOGE(tag, "Provisioning controls unavailable");
    } else
        ESP_LOGE(tag, "Wi-Fi init failed: %s", esp_err_to_name(network_status));
    monitor::Aggregator aggregator(boot_id);
    monitor::DisplayRuntime display;
    if (!display.initialize())
        ESP_LOGE(tag, "LCD unavailable; sensor/network services continue without display");
    monitor::DisplayTaskContext display_context{&display, &api, &wifi, xSemaphoreCreateMutex(),
                                                false,    {}};
    const bool display_task_started = display.ready() && display_context.mutex &&
                                      xTaskCreate(monitor::display_refresh_task, "display_refresh",
                                                  6144, &display_context, 3, nullptr) == pdPASS;
    if (display.ready() && !display_task_started)
        ESP_LOGE(tag, "Independent display refresh unavailable; using acquisition cadence");
    monitor::TrendRestore trend_restore;
    trend_restore.history = &history;
    trend_restore.mutex = xSemaphoreCreateMutex();
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
    monitor::DeviceLoopClock loop_clock;
    monitor::ApiLoopAdapter loop_api(api);
    monitor::SamplingLoopAdapter loop_sampling(sampling);
    monitor::HistoryLoopAdapter loop_history(history);
    monitor::NetworkLoopAdapter loop_network(wifi);
    monitor::DisplayLoopAdapter loop_display(display, display_context, display_task_started,
                                             device_settings);
    monitor::TrendRestoreLoopAdapter loop_trend_restore(trend_restore);
    monitor::OtaLoopAdapter loop_ota;
    monitor::LoopPorts loop_ports{loop_clock,   loop_api,     loop_sampling,      loop_history,
                                  loop_network, loop_display, loop_trend_restore, loop_ota};
    monitor::LoopConfig loop_config;
    loop_config.simulated = simulated;
    loop_config.boot_id = boot_id;
    loop_config.startup.history_worker_started = history_started;
    loop_config.startup.wifi_stack_started = network_status == ESP_OK;
    loop_config.startup.authentication_started = auth_started;
    loop_config.startup.http_server_started = http_started;
    loop_config.startup.sampling_initialized = bus_status == ESP_OK;
    monitor::MonitorLoop loop(loop_ports, registry, persistence, loop_config);
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        loop.run_cycle();
        const auto now = loop.cycle_started_ms();
        const auto simulated_tick = loop.cycle_tick();
        const auto &sensor_diagnostics = loop.diagnostics();
        const auto display_frames_dropped = loop.display_frames_dropped();
        if (!simulated || simulated_tick % 20 == 0) {
            const auto internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            const auto internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
            const auto internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
            const auto psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            const auto psram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
            const auto stack_min = uxTaskGetStackHighWaterMark(nullptr);
            ESP_LOGI(tag,
                     "HEALTH uptime_ms=%lld sensors=%u bus=%s clock=%s history=%d heap=%u "
                     "internal_free=%u internal_min=%u internal_largest=%u psram_free=%u "
                     "psram_min=%u main_stack_min=%u display_dropped=%u",
                     static_cast<long long>(now),
                     static_cast<unsigned>(loop.registry().sensors().size()),
                     simulated ? "SIMULATED"
                               : (sampling.enabled() ? (sampling.fault() ? "fault" : "ready")
                                                     : (sampling.fault() ? "fault" : "disabled")),
                     simulated
                         ? "simulated"
                         : (monitor::clock_synchronized() ? "synchronized" : "unsynchronized"),
                     int(history.healthy()), static_cast<unsigned>(esp_get_free_heap_size()),
                     static_cast<unsigned>(internal_free), static_cast<unsigned>(internal_min),
                     static_cast<unsigned>(internal_largest), static_cast<unsigned>(psram_free),
                     static_cast<unsigned>(psram_min), static_cast<unsigned>(stack_min),
                     display_frames_dropped);
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
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(simulated ? 250 : 5000));
    }
}
