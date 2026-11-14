#pragma once
// The acquisition loop (God file refactor, spec §5.2): one cycle every five seconds (250 ms in the
// simulation), as ordered steps over the device ports. The order is part of the behaviour:
//   1. take queued sensor edits (at most four) and commit them;
//   2. apply calibration holds;
//   3. read the probes and record diagnostics;
//   4. apply the readings to the registry;
//   5. write at most one probe memory record, never during a bus fault;
//   6. journal new alert events;
//   7. journal Wi-Fi and clock changes;
//   8. start or apply the trend restore;
//   9. aggregate and submit the five-minute bucket;
//  10. persist automatic settings changes, then set the API's writable flag;
//  11. publish to the API (after 10, so a rolled-back edit and `read_only` arrive together);
//  12. hand the frame to the display;
//  13. run the OTA boot health check.
// main.cpp logs the cycle's health and waits for the next one.
#include "loop_ports.hpp"
#include "sensor_diagnostics.hpp"
#include "sensor_settings_persistence.hpp"
namespace monitor {
struct LoopConfig {
    bool simulated = false;
    uint64_t boot_id = 0;
    StartupHealth startup;
};
class MonitorLoop {
  public:
    MonitorLoop(LoopPorts &ports, Registry &registry, SensorSettingsPersistence &persistence,
                const LoopConfig &config);
    void run_cycle();

    // For the health log.
    const Registry &registry() const {
        return registry_;
    }
    const SensorDiagnostics &diagnostics() const {
        return diagnostics_;
    }
    unsigned display_frames_dropped() const {
        return display_frames_dropped_;
    }
    // The tick of the cycle just run (the simulation logs its health every 20th).
    uint64_t cycle_tick() const {
        return cycle_.tick;
    }
    int64_t cycle_started_ms() const {
        return cycle_.now;
    }

  private:
    // What one cycle's steps pass along.
    struct Cycle {
        uint64_t tick = 0;      // the simulated tick this cycle runs at
        int64_t now = 0;        // monotonic ms at the start of the cycle
        int64_t sampled_at = 0; // the reading's time (the simulated tick in the simulation)
        std::size_t sensors_before = 0;
        bool automatic_change = false;
    };
    int64_t loop_time() const; // `now`, or the simulated tick's time
    void commit_sensor_edits();
    void apply_calibration_holds();
    void sample();
    void write_probe_memory();
    void journal_alerts();
    void journal_network_and_clock();
    void restore_trends();
    void aggregate();
    void persist_settings();
    void publish();
    void update_display();
    void check_ota_boot();

    LoopPorts &ports_;
    Registry &registry_;
    SensorSettingsPersistence &persistence_;
    LoopConfig config_;
    Aggregator aggregator_;
    SensorDiagnostics diagnostics_;
    TrendCache trend_cache_;
    // What the display last received, rebuilt only after trend_cache_ changes.
    std::shared_ptr<const TrendCache> trend_snapshot_ = std::make_shared<const TrendCache>();
    bool trend_changed_ = false;
    bool trend_restore_started_ = false, trend_restore_applied_ = false;
    unsigned display_frames_dropped_ = 0;
    uint64_t simulated_tick_ = 0;
    uint64_t last_event_sequence_ = 0;
    std::string prior_wifi_state_;
    bool prior_clock_synced_ = false;
    unsigned healthy_cycles_ = 0;
    bool ota_boot_checked_ = false;
    Cycle cycle_;
};
} // namespace monitor
