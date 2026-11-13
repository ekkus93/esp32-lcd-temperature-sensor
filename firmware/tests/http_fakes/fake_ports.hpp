#pragma once
// Recording fakes for the HTTP API ports (God file refactor G02.2). Every call is counted; the
// operations that change the device are also summed in side_effects(), so a test can show that a
// refused request reached none of them.
#include "api_ports.hpp"
#include "api_routes.hpp"
#include <map>
namespace monitor {
struct CallLog {
    std::map<std::string, int> calls;
    int count(const std::string &name) const {
        const auto found = calls.find(name);
        return found == calls.end() ? 0 : found->second;
    }
    void add(const char *name) {
        ++calls[name];
    }
};

class FakeAuth final : public AuthPort {
  public:
    mutable CallLog log;
    bool is_enrolled = true;
    EnrollResult enroll_result = EnrollResult::enrolled;
    LoginThrottle::Decision login_decision{LoginGate::allow, 0};
    std::optional<Session> session; // what authorize() and login_complete() return
    LoginFailure login_failure = LoginFailure::none;
    bool password_changed = true;
    std::string last_token, last_password;

    bool enrolled() const override {
        log.add("enrolled");
        return is_enrolled;
    }
    bool last_setup_open = false;
    EnrollResult enroll(const std::string &password, int64_t, bool setup_open) override {
        log.add("enroll");
        last_password = password;
        last_setup_open = setup_open;
        return enroll_result;
    }
    LoginThrottle::Decision login_begin(uint32_t, int64_t) override {
        log.add("login_begin");
        return login_decision;
    }
    void login_abort(uint32_t) override {
        log.add("login_abort");
    }
    std::optional<Session> login_complete(const std::string &password, uint32_t, int64_t,
                                          LoginFailure *failure) override {
        log.add("login_complete");
        last_password = password;
        if (failure)
            *failure = session ? LoginFailure::none : login_failure;
        return session;
    }
    std::optional<Session> authorize(const std::string &token, int64_t) override {
        log.add("authorize");
        last_token = token;
        return session && token == session->token ? session : std::nullopt;
    }
    void logout(const std::string &token) override {
        log.add("logout");
        last_token = token;
    }
    bool change_password(const std::string &token, const std::string &, const std::string &,
                         int64_t) override {
        log.add("change_password");
        last_token = token;
        return password_changed;
    }
    int side_effects() const {
        return log.count("enroll") + log.count("logout") + log.count("change_password");
    }
};

class FakeNetwork final : public NetworkPort {
  public:
    mutable CallLog log;
    NetworkInfo network{"connected", "Home", "192.168.1.20", false, "", 0, true, "", 0};
    std::string mdns = "temperature-monitor";
    bool busy = false, accepted = true, scanning = false;
    std::vector<ScannedNetwork> found;
    std::string last_ssid, last_rename;

    NetworkInfo info() const override {
        log.add("info");
        return network;
    }
    std::string mdns_hostname() const override {
        log.add("mdns_hostname");
        return mdns;
    }
    bool mdns_rename(const std::string &hostname) override {
        log.add("mdns_rename");
        last_rename = hostname;
        return accepted;
    }
    bool candidate_busy() const override {
        log.add("candidate_busy");
        return busy;
    }
    bool connect(const std::string &ssid, const std::string &) override {
        log.add("connect");
        last_ssid = ssid;
        return accepted;
    }
    bool provision() override {
        log.add("provision");
        return accepted;
    }
    bool scan() override {
        log.add("scan");
        return accepted;
    }
    bool scan_results(std::vector<ScannedNetwork> &networks, bool &now_scanning) override {
        log.add("scan_results");
        networks = found;
        now_scanning = scanning;
        return accepted;
    }
    int side_effects() const {
        return log.count("mdns_rename") + log.count("connect") + log.count("provision") +
               log.count("scan");
    }
};

class FakeHistory final : public HistoryPort {
  public:
    mutable CallLog log;
    bool available = true, truncated = false;
    std::vector<HistoryPoint> points;
    std::vector<HistoryGap> gaps;
    std::vector<Bucket> buckets;
    HistoryVisitEnd visit_end = HistoryVisitEnd::complete;
    JournalPage page;
    HistoryStorageInfo storage{3 * 1024 * 1024, 1024, true};

    bool query(const HistoryQuery &, std::vector<HistoryPoint> &out_points,
               std::vector<HistoryGap> &out_gaps, bool &out_truncated) override {
        log.add("query");
        out_points = points;
        out_gaps = gaps;
        out_truncated = truncated;
        return available;
    }
    HistoryVisitOutcome visit(const HistoryQuery &,
                              const std::function<bool(const Bucket &)> &visitor,
                              const HistoryVisitOptions &) override {
        log.add("visit");
        HistoryVisitOutcome outcome;
        outcome.end = visit_end;
        for (const auto &bucket : buckets)
            if (!visitor(bucket)) {
                outcome.end = HistoryVisitEnd::delivery_stopped;
                break;
            }
        return outcome;
    }
    JournalPage event_page(uint64_t, std::size_t) override {
        log.add("event_page");
        return page;
    }
    HistoryStorageInfo storage_info() override {
        log.add("storage_info");
        return storage;
    }
};

class FakeDeviceSettings final : public DeviceSettingsPort {
  public:
    mutable CallLog log;
    DeviceOptions options;
    uint64_t revision = 1;
    bool writable = true, available = true;
    esp_err_t save_result = ESP_OK;
    std::optional<DeviceOptions> saved;

    bool get(DeviceOptions &out, uint64_t &out_revision, bool &out_writable) override {
        log.add("get");
        out = options;
        out_revision = revision;
        out_writable = writable;
        return available;
    }
    esp_err_t save(const DeviceOptions &value, uint64_t) override {
        log.add("save");
        if (save_result == ESP_OK) {
            saved = value;
            options = value;
            ++revision;
        }
        return save_result;
    }
    int side_effects() const {
        return log.count("save");
    }
};

class FakeSystem final : public SystemPort {
  public:
    mutable CallLog log;
    OtaStatus ota{"0.2.0", "ota_0", "valid"};
    ClockDiagnostics clock;
    bool can_reboot = true, scheduled = true;
    std::optional<bool> reset_erase_history;

    OtaStatus ota_status() override {
        log.add("ota_status");
        return ota;
    }
    ClockDiagnostics clock_diagnostics() override {
        log.add("clock_diagnostics");
        return clock;
    }
    bool reboot_available() override {
        log.add("reboot_available");
        return can_reboot;
    }
    bool schedule_reboot() override {
        log.add("schedule_reboot");
        return scheduled;
    }
    void restart_now() override {
        log.add("restart_now");
    }
    bool schedule_factory_reset(bool erase_history) override {
        log.add("schedule_factory_reset");
        reset_erase_history = erase_history;
        return scheduled;
    }
    esp_err_t upload_firmware(HttpExchange &exchange) override {
        log.add("upload_firmware");
        exchange.set_status("202 Accepted");
        return exchange.send("{}", 2);
    }
    int side_effects() const {
        return log.count("schedule_reboot") + log.count("restart_now") +
               log.count("schedule_factory_reset") + log.count("upload_firmware");
    }
};

class FakeSensorUpdates final : public SensorUpdates {
  public:
    mutable CallLog log;
    bool writable = true, pending = false;
    QueueResult result = QueueResult::queued;
    struct Update {
        uint64_t rom;
        Settings settings;
        bool retire;
    };
    std::vector<Update> updates;
    struct Hold {
        uint64_t rom;
        int64_t duration_ms;
        bool cancel;
    };
    std::vector<Hold> holds;

    bool sensor_settings_writable() const override {
        log.add("sensor_settings_writable");
        return writable;
    }
    bool update_pending(uint64_t) const override {
        log.add("update_pending");
        return pending;
    }
    QueueResult queue_sensor_update(uint64_t rom, const Settings &settings, bool retire) override {
        log.add("queue_sensor_update");
        if (result == QueueResult::queued)
            updates.push_back({rom, settings, retire});
        return result;
    }
    QueueResult queue_calibration_hold(uint64_t rom, int64_t duration_ms, bool cancel) override {
        log.add("queue_calibration_hold");
        if (result == QueueResult::queued)
            holds.push_back({rom, duration_ms, cancel});
        return result;
    }
    int side_effects() const {
        return static_cast<int>(updates.size() + holds.size());
    }
};

class FakeLiveState final : public LiveState {
  public:
    mutable CallLog log;
    LiveView view;
    bool busy = false;
    std::string boot = "0123456789abcdef";
    bool is_simulated = false;

    bool read(LiveView &out) override {
        log.add("read");
        if (busy)
            return false;
        out = view;
        return true;
    }
    const std::string &boot_id() const override {
        return boot;
    }
    bool simulated() const override {
        return is_simulated;
    }
};

class FakeRouteHost final : public RouteHost {
  public:
    mutable CallLog log;
    bool accept = true;
    PublicLimiter::Decision admission{true, 0};
    // What was handed to the workers (kept alive so tests can inspect the async answers).
    std::vector<std::unique_ptr<HttpExchange>> queued;
    std::string last_password;
    uint32_t last_client = 0;
    std::optional<HistoryQuery> last_query;
    bool last_exporting = false;
    std::vector<PublicRequest> admitted, finished;
    std::string hostname;

    bool queue_login(std::unique_ptr<HttpExchange> exchange, std::string password,
                     uint32_t client) override {
        log.add("queue_login");
        if (!accept)
            return false; // the exchange is dropped, as the real host completes it
        last_password = std::move(password);
        last_client = client;
        queued.push_back(std::move(exchange));
        return true;
    }
    PublicLimiter::Decision admit_public(PublicRequest kind, uint32_t) override {
        log.add("admit_public");
        if (admission.allow)
            admitted.push_back(kind);
        return admission;
    }
    void finish_public(PublicRequest kind) override {
        log.add("finish_public");
        finished.push_back(kind);
    }
    bool queue_history(std::unique_ptr<HttpExchange> exchange, const HistoryQuery &query,
                       bool exporting, PublicRequest) override {
        log.add("queue_history");
        if (!accept)
            return false;
        last_query = query;
        last_exporting = exporting;
        queued.push_back(std::move(exchange));
        return true;
    }
    void apply_hostname(const std::string &name) override {
        log.add("apply_hostname");
        hostname = name;
    }
    int side_effects() const {
        return log.count("apply_hostname");
    }
};

// All fakes together, as a route group's tests use them.
struct FakeDevice {
    FakeAuth auth;
    FakeNetwork network;
    FakeHistory history;
    FakeDeviceSettings settings;
    FakeSystem system;
    FakeSensorUpdates updates;
    FakeLiveState live;
    FakeRouteHost host;
    RoutePorts ports{auth, network, history, settings, system, updates, live, host};
    // Calls that would have changed the device, summed over every port.
    int side_effects() const {
        return auth.side_effects() + network.side_effects() + settings.side_effects() +
               system.side_effects() + updates.side_effects() + host.side_effects();
    }
};
} // namespace monitor
