#pragma once
#include "api_ports.hpp"
#include "api_routes.hpp"
#include "auth_service.hpp"
#include "device_settings_service.hpp"
#include "esp_http_server.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "history_service.hpp"
#include "http_exchange.hpp"
#include "monitor.hpp"
#include "public_limiter.hpp"
#include "service_adapters.hpp"
#include "wifi_service.hpp"
#include "ws_live.hpp"
#include <mutex>
#include <optional>
#include <string>
namespace monitor {
// SensorUpdates and LiveState are implemented here: they are the queues and the published state
// shared with the sampling loop.
class HttpApi final : private SensorUpdates, private LiveState, private RouteHost {
  public:
    bool start(AuthService &auth, WifiService &wifi, HistoryService &history,
               DeviceSettingsService &settings, uint64_t boot_id, bool simulated = false,
               bool sensor_settings_writable = true);
    void set_sensor_settings_writable(bool writable) {
        sensor_settings_writable_.store(writable);
    }
    bool next_sensor_update(uint64_t &rom, Settings &settings, bool &retire);
    // The next calibration-hold request for the sampling loop: start (or renew) for
    // `duration_ms`, or end when `cancel`.
    bool next_calibration_hold(uint64_t &rom, int64_t &duration_ms, bool &cancel);
    bool publish(const Registry &registry, ProbeScanStatus probe_scan, int64_t uptime, bool logging,
                 bool synchronized, const std::string &utc);

  private:
    static esp_err_t dispatch(httpd_req_t *request);
    static esp_err_t live(httpd_req_t *request);
    static esp_err_t ws_pre(httpd_req_t *request);
    // Runs password checks off the HTTP task so a (slow) login never stalls other requests.
    static void login_worker(void *context);
    // Answers history queries and CSV exports off the HTTP task, so a month-long query or export
    // does not hold up every other request.
    static void history_worker(void *context);
    esp_err_t serve_history(HttpExchange &exchange, const HistoryQuery &query, bool exporting);
    PublicLimiter::Decision begin_public(PublicRequest kind, uint32_t client);
    void finish_public(PublicRequest kind) override;
    esp_err_t handle(HttpExchange &exchange);
    std::string snapshot();
    bool origin(HttpExchange &exchange, bool require_origin);
    std::optional<Session> session(HttpExchange &exchange, bool csrf);
    // SensorUpdates and LiveState (God file refactor, spec §4.3).
    bool sensor_settings_writable() const override {
        return sensor_settings_writable_.load();
    }
    bool update_pending(uint64_t view_generation) const override;
    QueueResult queue_sensor_update(uint64_t rom, const Settings &settings, bool retire) override;
    QueueResult queue_calibration_hold(uint64_t rom, int64_t duration_ms, bool cancel) override;
    bool read(LiveView &view) override;
    const std::string &boot_id() const override {
        return boot_id_;
    }
    bool simulated() const override {
        return simulated_;
    }
    // RouteHost: the workers, the public limiter and the hostname, for the route handlers.
    bool queue_login(std::unique_ptr<HttpExchange> exchange, std::string password,
                     uint32_t client) override;
    PublicLimiter::Decision admit_public(PublicRequest kind, uint32_t client) override {
        return begin_public(kind, client);
    }
    bool queue_history(std::unique_ptr<HttpExchange> exchange, const HistoryQuery &query,
                       bool exporting, PublicRequest kind) override;
    // The services, through the ports the routes use (created once in start()).
    std::unique_ptr<AuthAdapter> auth_adapter_;
    std::unique_ptr<NetworkAdapter> network_adapter_;
    std::unique_ptr<HistoryAdapter> history_adapter_;
    std::unique_ptr<DeviceSettingsAdapter> settings_adapter_;
    SystemAdapter system_adapter_;
    AuthPort *auth_ = nullptr;
    NetworkPort *wifi_ = nullptr;
    HistoryPort *history_ = nullptr;
    DeviceSettingsPort *settings_ = nullptr;
    SystemPort *system_ = &system_adapter_;
    SensorUpdates *updates_port_ = this;
    LiveState *live_ = this;
    std::unique_ptr<RoutePorts> ports_;
    SemaphoreHandle_t mutex_ = nullptr;
    struct SensorUpdate {
        uint64_t rom;
        Settings settings;
        uint64_t generation;
        bool retire;
    };
    QueueHandle_t updates_ = nullptr;
    struct CalibrationHoldUpdate {
        uint64_t rom;
        int64_t duration_ms;
        bool cancel;
    };
    QueueHandle_t holds_ = nullptr;
    struct LoginJob {
        std::unique_ptr<HttpExchange> exchange; // async; deleting the job completes the request
        std::string password;
        uint32_t client;
    };
    QueueHandle_t login_queue_ = nullptr;
    struct HistoryJob {
        std::unique_ptr<HttpExchange> exchange; // async; deleting the job completes the request
        HistoryQuery query;
        bool exporting;
        PublicRequest kind;
    };
    QueueHandle_t history_queue_ = nullptr;
    // Started on the HTTP task and finished on the history worker: always under limiter_mutex_.
    std::mutex limiter_mutex_;
    PublicLimiter limiter_;
    // The configured hostname (the Host allow-list follows it); changed by a settings update.
    std::mutex host_mutex_;
    std::string hostname_ = "temperature-monitor";
    void apply_hostname(const std::string &name) override;
    std::atomic<uint64_t> sent_generation_{0}, published_generation_{0};
    uint64_t consumed_generation_ = 0;
    Registry registry_;
    ProbeScanStatus probe_scan_{};
    int64_t uptime_ = 0;
    bool logging_ = false, synchronized_ = false;
    bool simulated_ = false;
    std::atomic<bool> sensor_settings_writable_{true};
    std::string utc_, boot_id_;
    httpd_handle_t server_ = nullptr;
    WsLive ws_live_;
};
} // namespace monitor
