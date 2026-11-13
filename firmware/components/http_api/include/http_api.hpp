#pragma once
#include "auth_service.hpp"
#include "device_settings_service.hpp"
#include "esp_http_server.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "history_service.hpp"
#include "monitor.hpp"
#include "public_limiter.hpp"
#include "wifi_service.hpp"
#include "ws_live.hpp"
#include <mutex>
#include <optional>
#include <string>
namespace monitor {
class HttpApi {
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
    esp_err_t serve_history(httpd_req_t *request, const HistoryQuery &query, bool exporting);
    PublicLimiter::Decision begin_public(PublicRequest kind, uint32_t client);
    void finish_public(PublicRequest kind);
    esp_err_t handle(httpd_req_t *request);
    std::string snapshot();
    bool origin(httpd_req_t *request, bool require_origin);
    std::optional<Session> session(httpd_req_t *request, bool csrf);
    AuthService *auth_ = nullptr;
    WifiService *wifi_ = nullptr;
    HistoryService *history_ = nullptr;
    DeviceSettingsService *settings_ = nullptr;
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
        httpd_req_t *request; // async copy, completed by the worker
        std::string password;
        uint32_t client;
    };
    QueueHandle_t login_queue_ = nullptr;
    struct HistoryJob {
        httpd_req_t *request; // async copy, completed by the history worker
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
    void apply_hostname(const std::string &name);
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
