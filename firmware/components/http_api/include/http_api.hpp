#pragma once
#include "auth_service.hpp"
#include "device_settings_service.hpp"
#include "esp_http_server.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "history_service.hpp"
#include "monitor.hpp"
#include "wifi_service.hpp"
#include "ws_live.hpp"
#include <string>
namespace monitor {
class HttpApi {
  public:
    bool start(AuthService &auth, WifiService &wifi, HistoryService &history,
               DeviceSettingsService &settings, uint64_t boot_id, bool simulated = false);
    bool next_sensor_update(uint64_t &rom, Settings &settings);
    void publish(const Registry &registry, int64_t uptime, bool logging, bool synchronized,
                 const std::string &utc);

  private:
    static esp_err_t dispatch(httpd_req_t *request);
    static esp_err_t live(httpd_req_t *request);
    static esp_err_t ws_pre(httpd_req_t *request);
    esp_err_t handle(httpd_req_t *request);
    std::string snapshot();
    bool origin(httpd_req_t *request, bool require_origin);
    Session *session(httpd_req_t *request, bool csrf);
    AuthService *auth_ = nullptr;
    WifiService *wifi_ = nullptr;
    HistoryService *history_ = nullptr;
    DeviceSettingsService *settings_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    struct SensorUpdate {
        uint64_t rom;
        Settings settings;
        uint64_t generation;
    };
    QueueHandle_t updates_ = nullptr;
    std::atomic<uint64_t> sent_generation_{0}, published_generation_{0};
    uint64_t consumed_generation_ = 0;
    Registry registry_;
    int64_t uptime_ = 0;
    bool logging_ = false, synchronized_ = false;
    bool simulated_ = false;
    std::string utc_, boot_id_;
    httpd_handle_t server_ = nullptr;
    WsLive ws_live_;
};
} // namespace monitor
