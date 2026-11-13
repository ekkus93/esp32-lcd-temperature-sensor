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
#include "sensor_settings_persistence.hpp"
#include "settings_service.hpp"
#include "wifi_service.hpp"
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace {
struct DisplayFrame {
    monitor::Registry registry;
    // Shared and never modified: the main loop hands over a new snapshot only when the trends
    // change (every five minutes), instead of copying a day of points every five seconds.
    std::shared_ptr<const monitor::TrendCache> trend_cache =
        std::make_shared<const monitor::TrendCache>();
    monitor::DeviceOptions options;
    monitor::DisplayStatus status;
    monitor::ProbeScanStatus scan_status;
    int64_t uptime_ms = 0;
    int64_t published_at_ms = 0;
    bool provisioning = false;
    bool logging = false, synchronized = false;
    std::string utc;
    std::string ssid, wifi_key;
    // "<name>.local" while the station is connected and mDNS is up; shown on the setup screen.
    std::string local_name;
};

// The last day of stored buckets for the trend graph. The history worker owns the store and a
// visit can take seconds, so it runs in its own task; the main loop places the buckets on this
// boot's timeline once the clock is trusted (the stored UTC starts are what places them).
struct TrendRestore {
    monitor::HistoryService *history = nullptr;
    SemaphoreHandle_t mutex = nullptr;
    monitor::TrendPoints stored;           // guarded by mutex; written by the task
    bool done = false;                     // guarded by mutex
    bool started = false, applied = false; // main loop only
};

void trend_restore_task(void *argument) {
    auto &restore = *static_cast<TrendRestore *>(argument);
    // Only what the graph draws is kept, in PSRAM: a day of full buckets used most of the free
    // internal RAM while it was collected.
    monitor::TrendPoints collected;
    if (const auto utc = monitor::trusted_utc()) {
        monitor::HistoryQuery query;
        query.from = (*utc - 86400) * 1000;
        query.to = (*utc + 1) * 1000;
        const auto outcome = restore.history->visit(query, [&](const monitor::Bucket &bucket) {
            collected.push_back(monitor::trend_point(bucket));
            return true;
        });
        if (!outcome.complete())
            ESP_LOGW("trends",
                     "Trend history restore read only part of the stored history (end %u, %u "
                     "damaged segment(s))",
                     unsigned(outcome.end), unsigned(outcome.skipped_segments));
    } else {
        ESP_LOGW("trends", "Trend history restore skipped: the clock is not trusted");
    }
    if (xSemaphoreTake(restore.mutex, portMAX_DELAY) == pdTRUE) {
        restore.stored = std::move(collected);
        restore.done = true;
        xSemaphoreGive(restore.mutex);
    }
    vTaskDelete(nullptr);
}

struct DisplayTaskContext {
    monitor::DisplayRuntime *display;
    monitor::HttpApi *api;
    monitor::WifiService *wifi;
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
            // Read directly so the setup screen appears within one 100 ms refresh of the AP
            // opening, not at the next 5 s acquisition frame.
            context.frame.provisioning = context.wifi && context.wifi->provisioning_open();
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
            context.display->update(context.frame.registry, *context.frame.trend_cache,
                                    context.frame.options, context.frame.status, uptime_ms,
                                    context.frame.provisioning, context.frame.ssid,
                                    context.frame.wifi_key, context.frame.local_name);
            xSemaphoreGive(context.mutex);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(100));
    }
}
} // namespace

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
    DisplayTaskContext display_context{&display, &api, &wifi, xSemaphoreCreateMutex(), false, {}};
    const bool display_task_started = display.ready() && display_context.mutex &&
                                      xTaskCreate(display_refresh_task, "display_refresh", 6144,
                                                  &display_context, 3, nullptr) == pdPASS;
    if (display.ready() && !display_task_started)
        ESP_LOGE(tag, "Independent display refresh unavailable; using acquisition cadence");
    monitor::TrendCache trend_cache;
    // What the display task last received, rebuilt only after trend_cache changes.
    std::shared_ptr<const monitor::TrendCache> trend_snapshot =
        std::make_shared<const monitor::TrendCache>();
    bool trend_changed = false;
    // Display frames not handed over because the display task held the frame for over 100 ms.
    unsigned display_frames_dropped = 0;
    TrendRestore trend_restore;
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
            if (changed)
                configuration_update = true;
        }
        if (configuration_update) {
            const auto saved = persistence.commit_edits(registry, committed_registry, now);
            if (saved != ESP_OK)
                ESP_LOGE(tag, "Sensor settings commit failed; edit rolled back: %s",
                         esp_err_to_name(saved));
        }
        // Ice-bath calibration holds (spec 17.3) are RAM only: they take effect from this cycle.
        uint64_t hold_rom = 0;
        int64_t hold_ms = 0;
        bool hold_cancel = false;
        while (api.next_calibration_hold(hold_rom, hold_ms, hold_cancel)) {
            const int64_t at = simulated ? int64_t(simulated_tick * 5000) : now;
            if (hold_cancel)
                registry.end_calibration_hold(hold_rom, at);
            else
                registry.hold_calibration(hold_rom, at, hold_ms);
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
        // A new probe, or one that brought its own calibration offset, is stored automatically.
        bool automatic_change =
            registry.sample(readings, sampled_at, sampling.fault(), sampling.monitored());
        // At most one probe memory write per cycle, between conversions, and none during a fault.
        if (!sampling.fault())
            if (const auto write = registry.next_probe_write()) {
                const auto held = sampling.store_probe_memory(write->rom, write->memory);
                registry.probe_written(write->rom, held);
                const auto &sensors = registry.sensors();
                const auto it = std::find_if(sensors.begin(), sensors.end(),
                                             [&](const auto &s) { return s.rom == write->rom; });
                if (it != sensors.end())
                    ESP_LOGI(tag, "Probe %s calibration memory: %s",
                             monitor::rom_hex(write->rom).c_str(),
                             monitor::probe_calibration_name(it->probe_calibration));
            }
        for (const auto &event : registry.events()) {
            if (event.sequence <= last_event_sequence)
                continue;
            monitor::JournalEvent record{};
            record.boot_id = boot_id;
            record.uptime_ms = event.uptime_ms;
            record.utc_seconds = simulated ? std::nullopt : monitor::trusted_utc();
            record.rom = event.rom;
            switch (event.type) {
            case monitor::EventType::state:
                record.kind = monitor::EventKind::sensor_state;
                break;
            case monitor::EventType::calibration_started:
                record.kind = monitor::EventKind::calibration_started;
                break;
            case monitor::EventType::calibration_ended:
                record.kind = monitor::EventKind::calibration_ended;
                break;
            }
            record.previous = event.previous;
            record.state = event.state;
            record.detail = event.detail;
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
        if (!simulated && trend_restore.mutex && !trend_restore.started &&
            monitor::clock_synchronized()) {
            trend_restore.started = true;
            if (xTaskCreate(trend_restore_task, "trend_restore", 8192, &trend_restore, 1,
                            nullptr) != pdPASS)
                ESP_LOGW(tag, "Trend history restore unavailable; the graph starts at boot");
        }
        if (trend_restore.started && !trend_restore.applied) {
            monitor::TrendPoints stored;
            bool done = false;
            if (xSemaphoreTake(trend_restore.mutex, 0) == pdTRUE) {
                done = trend_restore.done;
                if (done)
                    stored = std::move(trend_restore.stored);
                xSemaphoreGive(trend_restore.mutex);
            }
            if (done) {
                trend_restore.applied = true;
                if (const auto utc = monitor::trusted_utc()) {
                    trend_cache.restore(monitor::rebase_stored_trends(stored, *utc * 1000, now));
                    trend_changed = true;
                    ESP_LOGI(tag, "Trends restored from history");
                }
            }
        }
        if (auto bucket = aggregator.add(registry, sampled_at,
                                         simulated ? std::nullopt : monitor::trusted_utc())) {
            trend_cache.add(*bucket);
            trend_changed = true;
            if (!history.submit(*bucket))
                ESP_LOGE(tag, "History queue unavailable/full");
        }
        automatic_change = automatic_change || registry.sensors().size() != before;
        if (settings.writable()) {
            const auto saved = persistence.persist(registry, automatic_change, now);
            if (saved != ESP_OK && saved != ESP_ERR_NOT_FINISHED)
                ESP_LOGE(tag, "Settings save failed: %s; retrying after a pause",
                         esp_err_to_name(saved));
        }
        // Edits pause after a failed write and resume on their own; damaged stored data stays
        // read-only.
        api.set_sensor_settings_writable(persistence.accepting_edits(now));
        api.publish(registry, sampling.scan_status(), sampled_at, history.healthy(),
                    (!simulated && monitor::clock_synchronized()),
                    simulated ? std::string{} : monitor::utc_text());
        if (display.ready()) {
            monitor::DeviceOptions options;
            device_settings.get(options, device_revision, device_writable);
            const auto network = wifi.info();
            std::string local_name;
            if (network.state == "connected")
                if (const auto name = wifi.mdns_hostname(); !name.empty())
                    local_name = name + ".local";
            monitor::DisplayStatus display_status;
            display_status.wifi = network.state;
            display_status.local_time = simulated ? "" : monitor::local_time_text();
            display_status.logging = history.healthy();
            display_status.button_progress = monitor::boot_hold_progress();
            display_status.ota_state = monitor::ota_status().state;
            if (display_task_started) {
                if (trend_changed) {
                    trend_snapshot = std::make_shared<const monitor::TrendCache>(trend_cache);
                    trend_changed = false;
                }
                // Copy outside the lock and swap in, so the display task waits only for the swap;
                // the frame it replaces is freed after the lock is released.
                auto next_registry = registry;
                if (xSemaphoreTake(display_context.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    std::swap(display_context.frame.registry, next_registry);
                    display_context.frame.trend_cache = trend_snapshot;
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
                    display_context.frame.local_name = local_name;
                    xSemaphoreGive(display_context.mutex);
                } else {
                    ++display_frames_dropped;
                }
            } else {
                display.update(registry, trend_cache, options, display_status, sampled_at,
                               network.provisioning, wifi.ap_ssid(), wifi.ap_password(),
                               local_name);
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
                const auto action = monitor::ota_boot_decision(health, esp_timer_get_time() / 1000);
                if (action == monitor::OtaBootAction::roll_back) {
                    ESP_LOGE(tag,
                             "OTA image never became healthy; rolling back to the previous one");
                    // Returns only if there is no previous valid image to go back to.
                    esp_ota_mark_app_invalid_rollback_and_reboot();
                    ESP_LOGE(tag, "Rollback unavailable; the image stays pending");
                } else if (action == monitor::OtaBootAction::wait) {
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
            ESP_LOGI(tag,
                     "HEALTH uptime_ms=%lld sensors=%u bus=%s clock=%s history=%d heap=%u "
                     "internal_free=%u internal_min=%u internal_largest=%u psram_free=%u "
                     "psram_min=%u main_stack_min=%u display_dropped=%u",
                     static_cast<long long>(now), static_cast<unsigned>(registry.sensors().size()),
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
        ++simulated_tick;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(simulated ? 250 : 5000));
    }
}
