#include "http_api.hpp"
#include "api_responses.hpp"
#include "api_router.hpp"
#include "cJSON.h"
#include "calibration_hold_request.hpp"
#include "clock_service.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "history_csv.hpp"
#include "history_json.hpp"
#include "host_rules.hpp"
#include "idf_exchange.hpp"
#include "live_json.hpp"
#include "login_throttle.hpp"
#include "lwip/sockets.h"
#include "ota_service.hpp"
#include "public_limiter.hpp"
#include "retirement_policy.hpp"
#include "route_access.hpp"
#include "routes_auth.hpp"
#include "routes_history.hpp"
#include "routes_network.hpp"
#include "routes_sensors.hpp"
#include "routes_settings.hpp"
#include "routes_system.hpp"
#include "static_assets.hpp"
#include "web_assets.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <memory>
#include <sys/select.h>
#include <sys/socket.h>
#include <tuple>
#include <vector>
namespace monitor {
namespace {
int64_t now() {
    return esp_timer_get_time() / 1000;
}
} // namespace
bool HttpApi::origin(HttpExchange &ex, bool required) {
    auto host = ex.header("Host", 128);
    HostRules rules;
    rules.station_ip = wifi_->info().ip;
    {
        std::lock_guard<std::mutex> lock(host_mutex_);
        rules.configured_name = hostname_;
    }
    // After a name conflict mDNS runs under another name; accept that one too.
    rules.active_name = wifi_->mdns_hostname();
    if (!host_allowed(host, rules))
        return false;
    auto source = ex.header("Origin", 256);
    return source.empty() ? !required : source == "http://" + host;
}
void HttpApi::apply_hostname(const std::string &name) {
    {
        std::lock_guard<std::mutex> lock(host_mutex_);
        if (hostname_ == name)
            return;
        hostname_ = name;
    }
    // Rename without a reboot; existing sessions are unaffected.
    wifi_->mdns_rename(name);
}
std::optional<Session> HttpApi::session(HttpExchange &ex, bool csrf) {
    return request_session(ex, *auth_, csrf, now());
}
void HttpApi::login_worker(void *context) {
    auto &self = *static_cast<HttpApi *>(context);
    for (;;) {
        LoginJob *job = nullptr;
        if (xQueueReceive(self.login_queue_, &job, portMAX_DELAY) != pdTRUE || !job)
            continue;
        complete_login(*job->exchange, *self.auth_, job->password, job->client, now());
        delete job; // completes the request
    }
}
PublicLimiter::Decision HttpApi::begin_public(PublicRequest kind, uint32_t client) {
    std::lock_guard<std::mutex> lock(limiter_mutex_);
    return limiter_.begin(kind, client, now());
}
void HttpApi::finish_public(PublicRequest kind) {
    std::lock_guard<std::mutex> lock(limiter_mutex_);
    limiter_.finish(kind);
}
void HttpApi::history_worker(void *context) {
    auto &self = *static_cast<HttpApi *>(context);
    for (;;) {
        HistoryJob *job = nullptr;
        if (xQueueReceive(self.history_queue_, &job, portMAX_DELAY) != pdTRUE || !job)
            continue;
        // A failure after part of the answer was sent leaves the response unusable: close it,
        // as the HTTP server does when an ordinary handler fails.
        const int64_t started_us = esp_timer_get_time();
        const bool served =
            serve_history(*job->exchange, *self.ports_, job->query, job->exporting) == ESP_OK;
        if (!served)
            job->exchange->close_connection();
        // Total time on the worker: the history service's lock wait and run time (logged by it)
        // plus building and sending the response.
        ESP_LOGI("http", "History %s %s in %lld ms", job->exporting ? "export" : "query",
                 served ? "answered" : "failed",
                 (long long)((esp_timer_get_time() - started_us) / 1000));
        // The admission slot is returned on every outcome.
        self.finish_public(job->kind);
        delete job; // completes the request
    }
}
bool HttpApi::start(AuthService &auth, WifiService &wifi, HistoryService &history,
                    DeviceSettingsService &settings, uint64_t id, bool simulated,
                    bool sensor_settings_writable) {
    simulated_ = simulated;
    sensor_settings_writable_.store(sensor_settings_writable);
    auth_adapter_.reset(new (std::nothrow) AuthAdapter(auth));
    network_adapter_.reset(new (std::nothrow) NetworkAdapter(wifi));
    history_adapter_.reset(new (std::nothrow) HistoryAdapter(history));
    settings_adapter_.reset(new (std::nothrow) DeviceSettingsAdapter(settings));
    if (!auth_adapter_ || !network_adapter_ || !history_adapter_ || !settings_adapter_)
        return false;
    auth_ = auth_adapter_.get();
    wifi_ = network_adapter_.get();
    history_ = history_adapter_.get();
    settings_ = settings_adapter_.get();
    ports_.reset(new (std::nothrow) RoutePorts{*auth_, *wifi_, *history_, *settings_, *system_,
                                               *updates_port_, *live_, *this});
    if (!ports_)
        return false;
    {
        DeviceOptions options;
        uint64_t revision = 0;
        bool writable = false;
        if (settings.get(options, revision, writable)) {
            std::lock_guard<std::mutex> lock(host_mutex_);
            hostname_ = options.hostname;
        }
    }
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(id));
    boot_id_ = text;
    mutex_ = xSemaphoreCreateMutex();
    updates_ = xQueueCreate(4, sizeof(SensorUpdate *));
    holds_ = xQueueCreate(8, sizeof(CalibrationHoldUpdate *));
    login_queue_ = xQueueCreate(1, sizeof(LoginJob *)); // the throttle allows one derive at a time
    // The public limiter lets two history requests run at once; the worker takes them in turn.
    history_queue_ = xQueueCreate(PublicLimiter::max_concurrent, sizeof(HistoryJob *));
    if (!mutex_ || !updates_ || !holds_ || !login_queue_ || !history_queue_)
        return false;
    if (xTaskCreatePinnedToCore(login_worker, "login", 6144, this, 4, nullptr, 1) != pdPASS)
        return false;
    // Below the HTTP server's priority so it keeps answering while a query runs. The stack stays
    // in internal RAM: the worker reads the history store from flash.
    if (xTaskCreatePinnedToCore(history_worker, "history_http", 8192, this, 2, nullptr, 1) !=
        pdPASS)
        return false;
    cJSON_Hooks hooks{};
    hooks.malloc_fn = [](size_t size) -> void * {
        auto *memory = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return memory ? memory : std::malloc(size);
    };
    hooks.free_fn = std::free;
    cJSON_InitHooks(&hooks);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 12;
    // Four live clients (one reserved for the owner) plus two for requests. More sockets cost
    // internal heap: with six clients and eight sockets the free internal memory fell to 15 KB
    // under load on the board (docs/validation/device-public-access-2026-11-05.md).
    config.max_open_sockets = 6;
    config.lru_purge_enable = true;
    config.stack_size = 16384;
    config.core_id = 1; // Keep HTTP/KDF work off the CPU0 acquisition task.
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    if (httpd_start(&server_, &config) != ESP_OK)
        return false;
    ws_live_.configure(server_, auth, [this] { return snapshot(); });
    httpd_uri_t ws{};
    ws.uri = "/api/v1/live";
    ws.method = HTTP_GET;
    ws.handler = live;
    ws.user_ctx = this;
    ws.is_websocket = true;
    ws.ws_pre_handshake_cb = ws_pre;
    httpd_register_uri_handler(server_, &ws);
    for (auto method : {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_PATCH, HTTP_DELETE}) {
        httpd_uri_t route{};
        route.uri = "/*";
        route.method = method;
        route.handler = dispatch;
        route.user_ctx = this;
        httpd_register_uri_handler(server_, &route);
    }
    return true;
}
bool HttpApi::next_sensor_update(uint64_t &rom, Settings &settings, bool &retire) {
    SensorUpdate *update = nullptr;
    if (!updates_ || xQueueReceive(updates_, &update, 0) != pdTRUE)
        return false;
    consumed_generation_ = update->generation;
    rom = update->rom;
    settings = update->settings;
    retire = update->retire;
    delete update;
    return true;
}
bool HttpApi::next_calibration_hold(uint64_t &rom, int64_t &duration_ms, bool &cancel) {
    CalibrationHoldUpdate *update = nullptr;
    if (!holds_ || xQueueReceive(holds_, &update, 0) != pdTRUE)
        return false;
    rom = update->rom;
    duration_ms = update->duration_ms;
    cancel = update->cancel;
    delete update;
    return true;
}

bool HttpApi::queue_login(std::unique_ptr<HttpExchange> exchange, std::string password,
                          uint32_t client) {
    auto *job = new (std::nothrow) LoginJob{std::move(exchange), std::move(password), client};
    if (!job)
        return false;
    if (!login_queue_ || xQueueSend(login_queue_, &job, 0) != pdTRUE) {
        delete job; // completes the async copy
        return false;
    }
    return true;
}
bool HttpApi::queue_history(std::unique_ptr<HttpExchange> exchange, const HistoryQuery &query,
                            bool exporting, PublicRequest kind) {
    auto *job = new (std::nothrow) HistoryJob{std::move(exchange), query, exporting, kind};
    if (!job)
        return false;
    if (!history_queue_ || xQueueSend(history_queue_, &job, 0) != pdTRUE) {
        delete job; // completes the async copy
        return false;
    }
    return true;
}
bool HttpApi::update_pending(uint64_t view_generation) const {
    const auto published = published_generation_.load();
    return sent_generation_.load() != published || view_generation != published;
}
QueueResult HttpApi::queue_sensor_update(uint64_t rom, const Settings &settings, bool retire) {
    const uint64_t generation = sent_generation_.load() + 1;
    auto *update = new (std::nothrow) SensorUpdate{rom, settings, generation, retire};
    if (!update)
        return QueueResult::no_memory;
    if (xQueueSend(updates_, &update, 0) != pdTRUE) {
        delete update;
        return QueueResult::full;
    }
    sent_generation_ = generation;
    return QueueResult::queued;
}
QueueResult HttpApi::queue_calibration_hold(uint64_t rom, int64_t duration_ms, bool cancel) {
    auto *queued = new (std::nothrow) CalibrationHoldUpdate{rom, duration_ms, cancel};
    if (!queued)
        return QueueResult::no_memory;
    if (xQueueSend(holds_, &queued, 0) != pdTRUE) {
        delete queued;
        return QueueResult::full;
    }
    return QueueResult::queued;
}
bool HttpApi::read(LiveView &view) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE)
        return false;
    view.registry = registry_;
    view.probe_scan = probe_scan_;
    view.generation = published_generation_.load();
    view.uptime_ms = uptime_;
    view.logging = logging_;
    view.synchronized = synchronized_;
    view.utc = utc_;
    xSemaphoreGive(mutex_);
    return true;
}
bool HttpApi::publish(const Registry &registry, ProbeScanStatus probe_scan, int64_t uptime,
                      bool logging, bool synchronized, const std::string &utc) {
    if (!server_)
        return true;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        registry_ = registry;
        probe_scan_ = probe_scan;
        uptime_ = uptime;
        logging_ = logging;
        synchronized_ = synchronized;
        utc_ = utc;
        published_generation_ = consumed_generation_;
        xSemaphoreGive(mutex_);
    } else {
        return false;
    }
    ws_live_.schedule();
    return true;
}
std::string HttpApi::snapshot() {
    return snapshot_json(*ports_);
}
esp_err_t HttpApi::dispatch(httpd_req_t *request) {
    IdfExchange exchange(request);
    return static_cast<HttpApi *>(request->user_ctx)->handle(exchange);
}
esp_err_t HttpApi::ws_pre(httpd_req_t *r) {
    auto &self = *static_cast<HttpApi *>(r->user_ctx);
    IdfExchange ex(r);
    // Live readings are public, but the upgrade still needs the device's own Origin so another
    // site's page cannot read the stream. A session, when present, is kept (for the owner slot).
    auto session = self.session(ex, false);
    if (!self.origin(ex, true)) {
        send_error(ex, "403 Forbidden", "WebSocket authorization failed");
        return ESP_FAIL;
    }
    const int fd = httpd_req_to_sockfd(r);
    if (!self.ws_live_.add_client(fd, session ? session->token : std::string{})) {
        send_error(ex, "503 Service Unavailable", "Socket limit");
        return ESP_FAIL;
    }
    self.ws_live_.welcome();
    return ESP_OK;
}
esp_err_t HttpApi::live(httpd_req_t *r) {
    return static_cast<HttpApi *>(r->user_ctx)->ws_live_.receive(r);
}
esp_err_t HttpApi::handle(HttpExchange &ex) {
    std::string path = ex.uri();
    auto query = path.find('?');
    if (query != std::string::npos)
        path.resize(query);
    bool api = path.rfind("/api/", 0) == 0;
    if (const auto redirected = answer_portal_probe(ex, path, *wifi_))
        return *redirected;
    if (api && !origin(ex, ex.method() != HttpMethod::get))
        return send_error(ex, "403 Forbidden", "Invalid host/origin");
    // The open auth routes run before the gate: they manage the session themselves.
    if (api) {
        const std::optional<Session> no_session;
        RouteRequest early{ex, path, no_session, now()};
        if (path == "/api/v1/auth/state" && ex.method() == HttpMethod::get)
            return route_auth_state(early, *ports_);
        if (path == "/api/v1/auth/enroll" && ex.method() == HttpMethod::post)
            return route_auth_enroll(early, *ports_);
        if (path == "/api/v1/auth/login" && ex.method() == HttpMethod::post)
            return route_auth_login(early, *ports_);
    }
    if (api) {
        // One place decides who may call what: the route table and route_access (api_router.hpp).
        // A request reaches a handler below only when the gate says proceed; anything not in the
        // table is a 404 for everyone.
        const auto gate = gate_request(ex, *auth_, path, now());
        switch (gate.gate) {
        case ApiGate::not_found:
            return send_error(ex, "404 Not Found", "Unknown API route");
        case ApiGate::unauthorized:
            return send_error(ex, "401 Unauthorized", "Administrator session required");
        case ApiGate::proceed:
            break;
        }
        const auto &s = gate.session;
        RouteRequest request{ex, path, s, now()};
        switch (gate.route->id) {
        case RouteId::status:
            return route_status(request, *ports_);
        case RouteId::clock:
            return route_clock(request, *ports_);
        case RouteId::probe_scan:
            return route_probe_scan(request, *ports_);
        case RouteId::ota:
            return route_ota(request, *ports_);
        case RouteId::reboot:
            return route_reboot(request, *ports_);
        case RouteId::factory_reset:
            return route_factory_reset(request, *ports_);
        default:
            break;
        }
        if (gate.route->id == RouteId::settings_read || gate.route->id == RouteId::settings_update)
            return route_settings(request, *ports_);
        switch (gate.route->id) {
        case RouteId::wifi_status:
            return route_wifi_status(request, *ports_);
        case RouteId::wifi_scan_start:
            return route_wifi_scan_start(request, *ports_);
        case RouteId::wifi_scan_results:
            return route_wifi_scan_results(request, *ports_);
        case RouteId::wifi_connect:
            return route_wifi_connect(request, *ports_);
        case RouteId::wifi_provisioning:
            return route_wifi_provisioning(request, *ports_);
        default:
            break;
        }
        if (gate.route->id == RouteId::history || gate.route->id == RouteId::history_export)
            return route_history(request, *ports_);
        switch (gate.route->id) {
        case RouteId::alerts:
            return route_alerts(request, *ports_);
        case RouteId::sensors:
            return route_sensor_list(request, *ports_);
        case RouteId::sensor_storage:
            return route_sensor_storage(request, *ports_);
        case RouteId::calibration_hold_start:
        case RouteId::calibration_hold_end:
            return route_calibration_hold(request, *ports_);
        case RouteId::sensor_update:
        case RouteId::sensor_retire:
            return route_sensor_change(request, *ports_);
        default:
            break;
        }
        if (path == "/api/v1/auth/logout" && ex.method() == HttpMethod::post)
            return route_auth_logout(request, *ports_);
        if (path == "/api/v1/auth/password" && ex.method() == HttpMethod::post)
            return route_auth_password(request, *ports_);
        return send_error(ex, "404 Not Found", "Unknown API route");
    }
    return serve_static_asset(ex, path, web_assets, sizeof(web_assets) / sizeof(web_assets[0]));
}
} // namespace monitor
