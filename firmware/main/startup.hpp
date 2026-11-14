#pragma once
// Start-up as named steps, in order (God file refactor G16). Each keeps its old failure
// behaviour: log, keep running, never erase.
#include "auth_service.hpp"
#include "device_settings_service.hpp"
#include "display_task.hpp"
#include "history_service.hpp"
#include "http_api.hpp"
#include "loop_ports.hpp"
#include "sampling_service.hpp"
#include "settings_service.hpp"
#include "wifi_service.hpp"
namespace monitor {
// NVS, then a factory reset left pending by the last boot. Damaged NVS is reported, not erased.
void prepare_storage();
// The probes' settings into `registry`; damaged stored settings stay read-only and preserved.
void load_sensor_settings(SettingsService &settings, Registry &registry);
DeviceOptions load_device_settings(DeviceSettingsService &device_settings, bool simulated);
// The firmware version, ESP-IDF, memory and reset reason, for the log.
void log_build();
uint64_t new_boot_id();
bool start_history(HistoryService &history, bool simulated, uint64_t boot_id);
// Wi-Fi, then (when it started) the clock, authentication, the HTTP server, captive DNS and the
// USB recovery console with the BOOT button. Fills the network part of `startup`.
void start_network(WifiService &wifi, AuthService &auth, HttpApi &api, HistoryService &history,
                   DeviceSettingsService &device_settings, const DeviceOptions &options,
                   uint64_t boot_id, bool simulated, bool sensor_settings_writable,
                   StartupHealth &startup);
// The LCD and its refresh task; true when the task runs (else the loop draws directly).
bool start_display(DisplayRuntime &display, DisplayTaskContext &context);
bool start_sampling(SamplingService &sampling, bool simulated);
// The reboot record in the event journal.
void journal_boot(HistoryService &history, uint64_t boot_id, bool simulated);
} // namespace monitor
