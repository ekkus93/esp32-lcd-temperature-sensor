#pragma once
// What the HTTP routes need from the rest of the device (God file refactor, spec §4.3). Each port
// is exactly as wide as the calls the routes make. The device implementations are in
// service_adapters.hpp (over the services) and HttpApi itself (SensorUpdates, LiveState); host
// tests use recording fakes, so a refused request can be shown to have reached no service.
#include "auth_types.hpp"
#include "clock_diagnostics.hpp"
#include "device_options.hpp"
#include "event_store.hpp"
#include "history_query.hpp"
#include "history_storage_info.hpp"
#include "history_visit.hpp"
#include "http_exchange.hpp"
#include "login_throttle.hpp"
#include "monitor.hpp"
#include "network_types.hpp"
#include "ota_types.hpp"
#include "sessions.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace monitor {
class AuthPort {
  public:
    virtual ~AuthPort() = default;
    virtual bool enrolled() const = 0;
    virtual EnrollResult enroll(const std::string &password, int64_t now_ms,
                                bool setup_window_open) = 0;
    virtual LoginThrottle::Decision login_begin(uint32_t client, int64_t now_ms) = 0;
    virtual void login_abort(uint32_t client) = 0;
    // Slow (a few seconds): only on the login worker.
    virtual std::optional<Session> login_complete(const std::string &password, uint32_t client,
                                                  int64_t now_ms, LoginFailure *failure) = 0;
    virtual std::optional<Session> authorize(const std::string &token, int64_t now_ms) = 0;
    virtual void logout(const std::string &token) = 0;
    virtual bool change_password(const std::string &token, const std::string &old_password,
                                 const std::string &new_password, int64_t now_ms) = 0;
};
class NetworkPort {
  public:
    virtual ~NetworkPort() = default;
    virtual NetworkInfo info() const = 0;
    virtual std::string mdns_hostname() const = 0;
    virtual bool mdns_rename(const std::string &hostname) = 0;
    virtual bool candidate_busy() const = 0;
    virtual bool connect(const std::string &ssid, const std::string &password) = 0;
    virtual bool provision() = 0;
    virtual bool scan() = 0;
    virtual bool scan_results(std::vector<ScannedNetwork> &networks, bool &scanning) = 0;
};
class HistoryPort {
  public:
    virtual ~HistoryPort() = default;
    virtual bool query(const HistoryQuery &query, std::vector<HistoryPoint> &points,
                       std::vector<HistoryGap> &gaps, bool &truncated) = 0;
    virtual HistoryVisitOutcome visit(const HistoryQuery &query,
                                      const std::function<bool(const Bucket &)> &visitor,
                                      const HistoryVisitOptions &options) = 0;
    virtual JournalPage event_page(uint64_t after, std::size_t limit) = 0;
    virtual HistoryStorageInfo storage_info() = 0;
};
class DeviceSettingsPort {
  public:
    virtual ~DeviceSettingsPort() = default;
    virtual bool get(DeviceOptions &options, uint64_t &revision, bool &writable) = 0;
    virtual esp_err_t save(const DeviceOptions &options, uint64_t expected_revision) = 0;
};
class SystemPort {
  public:
    virtual ~SystemPort() = default;
    virtual OtaStatus ota_status() = 0;
    virtual ClockDiagnostics clock_diagnostics() = 0;
    virtual bool reboot_available() = 0;
    // Schedules a restart after the answer is sent; false if it could not be scheduled.
    virtual bool schedule_reboot() = 0;
    // Restarts at once (when scheduling failed after the answer was already sent).
    virtual void restart_now() = 0;
    virtual bool schedule_factory_reset(bool erase_history) = 0;
    // Streams a firmware image from the request into the inactive slot and answers it.
    virtual esp_err_t upload_firmware(HttpExchange &exchange) = 0;
};
// The owner's sensor edits and calibration holds, queued for the sampling loop.
enum class QueueResult { queued, no_memory, full };
class SensorUpdates {
  public:
    virtual ~SensorUpdates() = default;
    virtual bool sensor_settings_writable() const = 0;
    // An earlier edit has not been published yet, or the caller's view (taken at
    // `view_generation`) is older than the last published sample.
    virtual bool update_pending(uint64_t view_generation) const = 0;
    virtual QueueResult queue_sensor_update(uint64_t rom, const Settings &settings,
                                            bool retire) = 0;
    virtual QueueResult queue_calibration_hold(uint64_t rom, int64_t duration_ms, bool cancel) = 0;
};
// One consistent copy of what the sampling loop last published.
struct LiveView {
    Registry registry;
    ProbeScanStatus probe_scan{};
    uint64_t generation = 0;
    int64_t uptime_ms = 0;
    bool logging = false, synchronized = false;
    std::string utc;
};
class LiveState {
  public:
    virtual ~LiveState() = default;
    // False when the published state is busy (the caller answers 503).
    virtual bool read(LiveView &view) = 0;
    virtual const std::string &boot_id() const = 0;
    virtual bool simulated() const = 0;
};
} // namespace monitor
