#pragma once
// What the main loop needs from the device (God file refactor, spec §5.2). The device adapters
// are in loop_adapters.cpp; host tests use the recording fakes in tests/loop_fakes.
#include "device_options.hpp"
#include "display_model.hpp"
#include "esp_err.h"
#include "event_store.hpp"
#include "monitor.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace monitor {
// The monotonic clock and the trusted wall clock.
class LoopClock {
  public:
    virtual ~LoopClock() = default;
    virtual int64_t now_ms() = 0; // monotonic, since boot
    virtual std::optional<int64_t> trusted_utc() = 0;
    virtual bool synchronized() = 0;
    virtual std::string utc_text() = 0;
    virtual std::string local_time_text() = 0;
};
// The HTTP API's side of the loop: queued owner edits and holds in, published state out.
class LoopApi {
  public:
    virtual ~LoopApi() = default;
    virtual bool next_sensor_update(uint64_t &rom, Settings &settings, bool &retire) = 0;
    virtual bool next_calibration_hold(uint64_t &rom, int64_t &duration_ms, bool &cancel) = 0;
    virtual void set_sensor_settings_writable(bool writable) = 0;
    virtual bool publish(const Registry &registry, ProbeScanStatus probe_scan, int64_t uptime,
                         bool logging, bool synchronized, const std::string &utc) = 0;
};
// The 1-Wire bus.
class LoopSampling {
  public:
    virtual ~LoopSampling() = default;
    virtual std::vector<Reading> read(int64_t now_ms, const std::vector<uint64_t> &managed) = 0;
    virtual std::optional<ProbeMemory> store_probe_memory(uint64_t rom, ProbeMemory memory) = 0;
    virtual bool fault() const = 0;
    virtual bool enabled() const = 0;
    virtual bool monitored() const = 0;
    virtual ProbeScanStatus scan_status() const = 0;
};
// The history worker's queues.
class LoopHistory {
  public:
    virtual ~LoopHistory() = default;
    virtual bool submit(const Bucket &bucket) = 0;
    virtual bool submit_event(const JournalEvent &event) = 0;
    virtual bool healthy() const = 0;
};
// Wi-Fi as the loop and the LCD see it.
struct LoopNetworkInfo {
    std::string state;
    bool provisioning = false;
};
class LoopNetwork {
  public:
    virtual ~LoopNetwork() = default;
    virtual LoopNetworkInfo info() const = 0;
    virtual std::string mdns_hostname() const = 0;
    virtual std::string ap_ssid() const = 0;
    virtual std::string ap_password() const = 0;
};
// Everything the LCD draws for one acquisition cycle.
struct DisplayFrame {
    Registry registry;
    // Shared and never modified: the main loop hands over a new snapshot only when the trends
    // change (every five minutes), instead of copying a day of points every five seconds.
    std::shared_ptr<const TrendCache> trend_cache = std::make_shared<const TrendCache>();
    DeviceOptions options;
    DisplayStatus status;
    ProbeScanStatus scan_status;
    int64_t uptime_ms = 0;
    int64_t published_at_ms = 0;
    bool provisioning = false;
    bool logging = false, synchronized = false;
    std::string utc;
    std::string ssid, wifi_key;
    // "<name>.local" while the station is connected and mDNS is up; shown on the setup screen.
    std::string local_name;
};
class LoopDisplay {
  public:
    virtual ~LoopDisplay() = default;
    virtual bool ready() const = 0;
    // Hands the frame to the display (its refresh task, or a direct draw without one). False when
    // the display task held the previous frame too long: this frame is dropped.
    virtual bool hand_over(DisplayFrame &&frame) = 0;
    virtual DeviceOptions options() = 0;    // the LCD settings to draw with
    virtual unsigned button_progress() = 0; // BOOT hold progress, for the LCD
    virtual std::string ota_state() = 0;
};
// The one-shot task that reads the last day of stored buckets for the trend graph.
class LoopTrendRestore {
  public:
    virtual ~LoopTrendRestore() = default;
    virtual bool available() const = 0; // the restore can run at all
    virtual bool start() = 0;           // false if the task could not start
    // The stored points once the task has finished; nothing while it is still reading.
    virtual std::optional<TrendPoints> take() = 0;
};
// The OTA boot check: a new image is confirmed only after healthy cycles.
class LoopOta {
  public:
    virtual ~LoopOta() = default;
    // The running image is waiting for confirmation (false also when its state is unknown).
    virtual bool pending_verify() = 0;
    // Returns only if there is no previous valid image to go back to.
    virtual void roll_back() = 0;
    virtual esp_err_t mark_valid() = 0;
};
// Which start-up services came up (for the OTA boot decision).
struct StartupHealth {
    bool history_worker_started = false;
    bool wifi_stack_started = false;
    bool authentication_started = false;
    bool http_server_started = false;
    bool sampling_initialized = false;
};
struct LoopPorts {
    LoopClock &clock;
    LoopApi &api;
    LoopSampling &sampling;
    LoopHistory &history;
    LoopNetwork &network;
    LoopDisplay &display;
    LoopTrendRestore &trend_restore;
    LoopOta &ota;
};
} // namespace monitor
