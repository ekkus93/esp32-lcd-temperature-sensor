// The acquisition loop's steps (moved from app_main, God file refactor G15).
#include "monitor_loop.hpp"
#include "esp_log.h"
#include "ota_boot_policy.hpp"
#include <algorithm>
namespace monitor {
namespace {
const char *tag = "monitor";
uint32_t wifi_code(const std::string &state) {
    if (state == "connecting")
        return 1;
    if (state == "connected")
        return 2;
    if (state == "setup")
        return 3;
    return 0;
}
} // namespace

MonitorLoop::MonitorLoop(LoopPorts &ports, Registry &registry,
                         SensorSettingsPersistence &persistence, const LoopConfig &config)
    : ports_(ports), registry_(registry), persistence_(persistence), config_(config),
      aggregator_(config.boot_id), prior_wifi_state_(ports.network.info().state) {}

int64_t MonitorLoop::loop_time() const {
    return config_.simulated ? int64_t(simulated_tick_ * 5000) : cycle_.now;
}
void MonitorLoop::run_cycle() {
    cycle_ = Cycle{};
    cycle_.tick = simulated_tick_;
    cycle_.now = ports_.clock.now_ms();
    commit_sensor_edits();
    apply_calibration_holds();
    sample();
    write_probe_memory();
    journal_alerts();
    journal_network_and_clock();
    restore_trends();
    aggregate();
    persist_settings();
    publish();
    update_display();
    check_ota_boot();
    ++simulated_tick_;
}
void MonitorLoop::commit_sensor_edits() {
    uint64_t changed_rom = 0;
    Settings changed_settings;
    auto committed_registry = registry_;
    bool configuration_update = false;
    bool retire_update = false;
    for (unsigned update = 0;
         update < 4 && ports_.api.next_sensor_update(changed_rom, changed_settings, retire_update);
         ++update) {
        const bool changed = retire_update
                                 ? registry_.retire(changed_rom)
                                 : registry_.configure(changed_rom, changed_settings, loop_time());
        if (changed)
            configuration_update = true;
    }
    if (configuration_update) {
        const auto saved = persistence_.commit_edits(registry_, committed_registry, cycle_.now);
        if (saved != ESP_OK)
            ESP_LOGE(tag, "Sensor settings commit failed; edit rolled back: %s",
                     esp_err_to_name(saved));
    }
}
// Ice-bath calibration holds (spec 17.3) are RAM only: they take effect from this cycle.
void MonitorLoop::apply_calibration_holds() {
    uint64_t hold_rom = 0;
    int64_t hold_ms = 0;
    bool hold_cancel = false;
    while (ports_.api.next_calibration_hold(hold_rom, hold_ms, hold_cancel)) {
        if (hold_cancel)
            registry_.end_calibration_hold(hold_rom, loop_time());
        else
            registry_.hold_calibration(hold_rom, loop_time(), hold_ms);
    }
}
void MonitorLoop::sample() {
    cycle_.sensors_before = registry_.sensors().size();
    std::vector<uint64_t> managed_roms;
    managed_roms.reserve(registry_.sensors().size());
    for (const auto &sensor : registry_.sensors())
        managed_roms.push_back(sensor.rom);
    auto &sampling = ports_.sampling;
    const int64_t read_started_ms = ports_.clock.now_ms();
    const auto readings = sampling.read(cycle_.now, managed_roms);
    const int64_t read_ended_ms = ports_.clock.now_ms();
    if (sampling.enabled())
        diagnostics_.observe(readings, sampling.fault(), read_ended_ms - read_started_ms);
    cycle_.sampled_at = config_.simulated ? int64_t(simulated_tick_ * 5000) : read_ended_ms;
    // A new probe, or one that brought its own calibration offset, is stored automatically.
    cycle_.automatic_change =
        registry_.sample(readings, cycle_.sampled_at, sampling.fault(), sampling.monitored());
}
// At most one probe memory write per cycle, between conversions, and none during a fault.
void MonitorLoop::write_probe_memory() {
    if (ports_.sampling.fault())
        return;
    const auto write = registry_.next_probe_write();
    if (!write)
        return;
    const auto held = ports_.sampling.store_probe_memory(write->rom, write->memory);
    registry_.probe_written(write->rom, held);
    const auto &sensors = registry_.sensors();
    const auto it = std::find_if(sensors.begin(), sensors.end(),
                                 [&](const auto &s) { return s.rom == write->rom; });
    if (it != sensors.end())
        ESP_LOGI(tag, "Probe %s calibration memory: %s", rom_hex(write->rom).c_str(),
                 probe_calibration_name(it->probe_calibration));
}
void MonitorLoop::journal_alerts() {
    for (const auto &event : registry_.events()) {
        if (event.sequence <= last_event_sequence_)
            continue;
        JournalEvent record{};
        record.boot_id = config_.boot_id;
        record.uptime_ms = event.uptime_ms;
        record.utc_seconds = config_.simulated ? std::nullopt : ports_.clock.trusted_utc();
        record.rom = event.rom;
        switch (event.type) {
        case EventType::state:
            record.kind = EventKind::sensor_state;
            break;
        case EventType::calibration_started:
            record.kind = EventKind::calibration_started;
            break;
        case EventType::calibration_ended:
            record.kind = EventKind::calibration_ended;
            break;
        }
        record.previous = event.previous;
        record.state = event.state;
        record.detail = event.detail;
        if (!ports_.history.submit_event(record))
            ESP_LOGE(tag, "Event queue full; acquisition continues");
        last_event_sequence_ = event.sequence;
    }
}
void MonitorLoop::journal_network_and_clock() {
    const auto network = ports_.network.info();
    if (network.state != prior_wifi_state_) {
        JournalEvent event{};
        event.boot_id = config_.boot_id;
        event.uptime_ms = cycle_.sampled_at;
        event.utc_seconds = config_.simulated ? std::nullopt : ports_.clock.trusted_utc();
        event.kind = EventKind::wifi_state;
        event.detail = (wifi_code(prior_wifi_state_) << 8) | wifi_code(network.state);
        ports_.history.submit_event(event);
        prior_wifi_state_ = network.state;
    }
    const bool clock_synced = !config_.simulated && ports_.clock.synchronized();
    if (clock_synced != prior_clock_synced_) {
        JournalEvent event{};
        event.boot_id = config_.boot_id;
        event.uptime_ms = cycle_.sampled_at;
        event.utc_seconds = clock_synced ? ports_.clock.trusted_utc() : std::nullopt;
        event.kind = EventKind::clock_sync;
        event.detail = clock_synced ? 1 : 0;
        ports_.history.submit_event(event);
        prior_clock_synced_ = clock_synced;
    }
}
void MonitorLoop::restore_trends() {
    auto &restore = ports_.trend_restore;
    if (!config_.simulated && restore.available() && !trend_restore_started_ &&
        ports_.clock.synchronized()) {
        trend_restore_started_ = true;
        if (!restore.start())
            ESP_LOGW(tag, "Trend history restore unavailable; the graph starts at boot");
    }
    if (!trend_restore_started_ || trend_restore_applied_)
        return;
    const auto stored = restore.take();
    if (!stored)
        return;
    trend_restore_applied_ = true;
    if (const auto utc = ports_.clock.trusted_utc()) {
        trend_cache_.restore(rebase_stored_trends(*stored, *utc * 1000, cycle_.now));
        trend_changed_ = true;
        ESP_LOGI(tag, "Trends restored from history");
    }
}
void MonitorLoop::aggregate() {
    const auto utc = config_.simulated ? std::nullopt : ports_.clock.trusted_utc();
    if (auto bucket = aggregator_.add(registry_, cycle_.sampled_at, utc)) {
        trend_cache_.add(*bucket);
        trend_changed_ = true;
        if (!ports_.history.submit(*bucket))
            ESP_LOGE(tag, "History queue unavailable/full");
    }
}
void MonitorLoop::persist_settings() {
    cycle_.automatic_change =
        cycle_.automatic_change || registry_.sensors().size() != cycle_.sensors_before;
    if (persistence_.writable()) {
        const auto saved = persistence_.persist(registry_, cycle_.automatic_change, cycle_.now);
        if (saved != ESP_OK && saved != ESP_ERR_NOT_FINISHED)
            ESP_LOGE(tag, "Settings save failed: %s; retrying after a pause",
                     esp_err_to_name(saved));
    }
    // Edits pause after a failed write and resume on their own; damaged stored data stays
    // read-only.
    ports_.api.set_sensor_settings_writable(persistence_.accepting_edits(cycle_.now));
}
void MonitorLoop::publish() {
    const bool simulated = config_.simulated;
    ports_.api.publish(registry_, ports_.sampling.scan_status(), cycle_.sampled_at,
                       ports_.history.healthy(), !simulated && ports_.clock.synchronized(),
                       simulated ? std::string{} : ports_.clock.utc_text());
}
void MonitorLoop::update_display() {
    auto &display = ports_.display;
    if (!display.ready())
        return;
    const bool simulated = config_.simulated;
    const auto network = ports_.network.info();
    DisplayFrame frame;
    frame.options = display.options();
    if (network.state == "connected")
        if (const auto name = ports_.network.mdns_hostname(); !name.empty())
            frame.local_name = name + ".local";
    frame.status.wifi = network.state;
    frame.status.local_time = simulated ? "" : ports_.clock.local_time_text();
    frame.status.logging = ports_.history.healthy();
    frame.status.button_progress = display.button_progress();
    frame.status.ota_state = display.ota_state();
    if (trend_changed_) {
        trend_snapshot_ = std::make_shared<const TrendCache>(trend_cache_);
        trend_changed_ = false;
    }
    frame.trend_cache = trend_snapshot_;
    frame.registry = registry_;
    frame.scan_status = ports_.sampling.scan_status();
    frame.uptime_ms = cycle_.sampled_at;
    frame.published_at_ms = ports_.clock.now_ms();
    frame.logging = ports_.history.healthy();
    frame.synchronized = !simulated && ports_.clock.synchronized();
    frame.utc = simulated ? std::string{} : ports_.clock.utc_text();
    frame.provisioning = network.provisioning;
    frame.ssid = ports_.network.ap_ssid();
    frame.wifi_key = ports_.network.ap_password();
    if (!display.hand_over(std::move(frame)))
        ++display_frames_dropped_;
}
void MonitorLoop::check_ota_boot() {
    if (config_.simulated || ota_boot_checked_ || ++healthy_cycles_ < 2)
        return;
    auto &ota = ports_.ota;
    if (!ota.pending_verify()) {
        ota_boot_checked_ = true;
        return;
    }
    OtaBootHealth health;
    health.completed_cycles = healthy_cycles_;
    health.history_worker_started = config_.startup.history_worker_started;
    health.wifi_stack_started = config_.startup.wifi_stack_started;
    health.authentication_started = config_.startup.authentication_started;
    health.http_server_started = config_.startup.http_server_started;
    health.sampling_initialized = config_.startup.sampling_initialized;
    const auto action = ota_boot_decision(health, ports_.clock.now_ms());
    if (action == OtaBootAction::roll_back) {
        ESP_LOGE(tag, "OTA image never became healthy; rolling back to the previous one");
        ota.roll_back(); // returns only if there is no previous valid image to go back to
        ESP_LOGE(tag, "Rollback unavailable; the image stays pending");
    } else if (action == OtaBootAction::wait) {
        if (healthy_cycles_ == 2)
            ESP_LOGE(tag, "OTA startup services incomplete; image remains pending");
    } else {
        const esp_err_t valid = ota.mark_valid();
        if (valid == ESP_OK) {
            ESP_LOGI(tag, "OTA startup health passed; rollback cancelled");
            ota_boot_checked_ = true;
        } else {
            ESP_LOGE(tag, "OTA image remains pending verification: %s", esp_err_to_name(valid));
        }
    }
}
} // namespace monitor
