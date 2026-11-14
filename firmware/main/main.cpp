// The firmware's entry point (God file refactor G16): start-up in order (startup.cpp), then the
// acquisition loop (monitor_loop.cpp) every five seconds (250 ms in the simulation), with a health
// log after each cycle (health_log.cpp). Every object here lives as long as the device runs.
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "health_log.hpp"
#include "loop_adapters.hpp"
#include "monitor_loop.hpp"
#include "sensor_settings_persistence.hpp"
#include "startup.hpp"

extern "C" void app_main() {
#ifdef CONFIG_MONITOR_SIMULATION
    constexpr bool simulated = true;
#else
    constexpr bool simulated = false;
#endif
    using namespace monitor;
    prepare_storage();
    Registry registry;
    SettingsService settings(simulated ? "monitor_sim" : "temp_monitor");
    load_sensor_settings(settings, registry);
    SensorSettingsPersistence persistence(settings);
    DeviceSettingsService device_settings;
    const auto device_options = load_device_settings(device_settings, simulated);
    log_build();
    const uint64_t boot_id = new_boot_id();
    LoopConfig config;
    config.simulated = simulated;
    config.boot_id = boot_id;
    HistoryService history;
    config.startup.history_worker_started = start_history(history, simulated, boot_id);
    WifiService wifi;
    AuthService auth;
    HttpApi api;
    start_network(wifi, auth, api, history, device_settings, device_options, boot_id, simulated,
                  settings.writable(), config.startup);
    DisplayRuntime display;
    DisplayTaskContext display_context{&display, &api, &wifi, xSemaphoreCreateMutex(), false, {}};
    const bool display_task_started = start_display(display, display_context);
    TrendRestore trend_restore;
    trend_restore.history = &history;
    trend_restore.mutex = xSemaphoreCreateMutex();
    SamplingService sampling;
    config.startup.sampling_initialized = start_sampling(sampling, simulated);
    journal_boot(history, boot_id, simulated);

    DeviceLoopClock clock;
    ApiLoopAdapter loop_api(api);
    SamplingLoopAdapter loop_sampling(sampling);
    HistoryLoopAdapter loop_history(history);
    NetworkLoopAdapter loop_network(wifi);
    DisplayLoopAdapter loop_display(display, display_context, display_task_started,
                                    device_settings);
    TrendRestoreLoopAdapter loop_trend_restore(trend_restore);
    OtaLoopAdapter loop_ota;
    LoopPorts ports{clock,        loop_api,     loop_sampling,      loop_history,
                    loop_network, loop_display, loop_trend_restore, loop_ota};
    MonitorLoop loop(ports, registry, persistence, config);
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        loop.run_cycle();
        log_health(loop, sampling, history, simulated);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(simulated ? 250 : 5000));
    }
}
