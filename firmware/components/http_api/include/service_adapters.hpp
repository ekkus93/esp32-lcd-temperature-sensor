#pragma once
// Device implementations of the HTTP API ports over the services (God file refactor, spec §4.3).
#include "api_ports.hpp"
#include "auth_service.hpp"
#include "device_settings_service.hpp"
#include "history_service.hpp"
#include "wifi_service.hpp"
namespace monitor {
class AuthAdapter final : public AuthPort {
  public:
    explicit AuthAdapter(AuthService &service) : service_(service) {}
    bool enrolled() const override {
        return service_.enrolled();
    }
    EnrollResult enroll(const std::string &password, int64_t now_ms,
                        bool setup_window_open) override {
        return service_.enroll(password, now_ms, setup_window_open);
    }
    LoginThrottle::Decision login_begin(uint32_t client, int64_t now_ms) override {
        return service_.login_begin(client, now_ms);
    }
    void login_abort(uint32_t client) override {
        service_.login_abort(client);
    }
    std::optional<Session> login_complete(const std::string &password, uint32_t client,
                                          int64_t now_ms, LoginFailure *failure) override {
        return service_.login_complete(password, client, now_ms, failure);
    }
    std::optional<Session> authorize(const std::string &token, int64_t now_ms) override {
        return service_.authorize(token, now_ms);
    }
    void logout(const std::string &token) override {
        service_.logout(token);
    }
    bool change_password(const std::string &token, const std::string &old_password,
                         const std::string &new_password, int64_t now_ms) override {
        return service_.change_password(token, old_password, new_password, now_ms);
    }

  private:
    AuthService &service_;
};
class NetworkAdapter final : public NetworkPort {
  public:
    explicit NetworkAdapter(WifiService &service) : service_(service) {}
    NetworkInfo info() const override {
        return service_.info();
    }
    std::string mdns_hostname() const override {
        return service_.mdns_hostname();
    }
    bool mdns_rename(const std::string &hostname) override {
        return service_.mdns_rename(hostname);
    }
    bool candidate_busy() const override {
        return service_.candidate_busy();
    }
    bool connect(const std::string &ssid, const std::string &password) override {
        return service_.connect(ssid, password);
    }
    bool provision() override {
        return service_.provision();
    }
    bool scan() override {
        return service_.scan();
    }
    bool scan_results(std::vector<ScannedNetwork> &networks, bool &scanning) override {
        return service_.scan_results(networks, scanning);
    }

  private:
    WifiService &service_;
};
class HistoryAdapter final : public HistoryPort {
  public:
    explicit HistoryAdapter(HistoryService &service) : service_(service) {}
    bool query(const HistoryQuery &query, std::vector<HistoryPoint> &points,
               std::vector<HistoryGap> &gaps, bool &truncated) override {
        return service_.query(query, points, gaps, truncated);
    }
    HistoryVisitOutcome visit(const HistoryQuery &query,
                              const std::function<bool(const Bucket &)> &visitor,
                              const HistoryVisitOptions &options) override {
        return service_.visit(query, visitor, options);
    }
    JournalPage event_page(uint64_t after, std::size_t limit) override {
        return service_.event_page(after, limit);
    }
    HistoryStorageInfo storage_info() override {
        return service_.storage_info();
    }

  private:
    HistoryService &service_;
};
class DeviceSettingsAdapter final : public DeviceSettingsPort {
  public:
    explicit DeviceSettingsAdapter(DeviceSettingsService &service) : service_(service) {}
    bool get(DeviceOptions &options, uint64_t &revision, bool &writable) override {
        return service_.get(options, revision, writable);
    }
    esp_err_t save(const DeviceOptions &options, uint64_t expected_revision) override {
        return service_.save(options, expected_revision);
    }

  private:
    DeviceSettingsService &service_;
};
// Reboot, factory reset, OTA and clock diagnostics (ota_service and clock_service).
class SystemAdapter final : public SystemPort {
  public:
    OtaStatus ota_status() override;
    ClockDiagnostics clock_diagnostics() override;
    std::optional<int64_t> trusted_utc() override;
    bool configure_clock(const DeviceOptions &options) override;
    bool reboot_available() override;
    bool schedule_reboot() override;
    void restart_now() override;
    bool schedule_factory_reset(bool erase_history) override;
    esp_err_t upload_firmware(HttpExchange &exchange) override;
};
} // namespace monitor
