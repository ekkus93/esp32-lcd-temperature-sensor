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
#include "routes_network.hpp"
#include "routes_sensors.hpp"
#include "routes_settings.hpp"
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
// Answers one history query or CSV export on the history worker (never on the HTTP task).
esp_err_t HttpApi::serve_history(HttpExchange &ex, const HistoryQuery &query, bool exporting) {
    if (exporting) {
        // The HTTP response as the export's destination.
        struct HttpCsvSink final : CsvExportSink {
            HttpExchange &exchange;
            explicit HttpCsvSink(HttpExchange &e) : exchange(e) {}
            bool begin() override {
                exchange.set_type("text/csv; charset=utf-8");
                exchange.set_header("Content-Disposition",
                                    "attachment; filename=temperature-history.csv");
                exchange.set_header("Cache-Control", "no-store");
                const char *header = history_csv_header();
                return exchange.send_chunk(header, std::strlen(header)) == ESP_OK;
            }
            bool send(const char *data, std::size_t size) override {
                return exchange.send_chunk(data, size) == ESP_OK;
            }
            bool finish() override {
                return exchange.send_chunk(nullptr, 0) == ESP_OK;
            }
        } sink(ex);
        const auto result = export_history_csv(
            [&](const std::function<bool(const Bucket &)> &visitor,
                const HistoryVisitOptions &options) {
                return history_->visit(query, visitor, options);
            },
            query.roms, simulated_, sink);
        switch (result.end) {
        case CsvExportEnd::complete:
            return ESP_OK;
        case CsvExportEnd::refused:
            switch (result.visit.end) {
            case HistoryVisitEnd::skipped_damaged:
                return send_error(
                    ex, "503 Service Unavailable",
                    "Part of the stored history in this range is damaged, so a complete "
                    "export is not possible. The chart still shows what can be read.");
            case HistoryVisitEnd::deadline:
                return send_error(ex, "503 Service Unavailable",
                                  "The export took too long. Try a shorter range.");
            default:
                return send_error(ex, "503 Service Unavailable", "History unavailable or busy");
            }
        case CsvExportEnd::aborted:
            break;
        }
        // Rows were already sent: close the connection without the final chunk, so the download
        // fails instead of looking complete.
        ESP_LOGW("http",
                 "History export incomplete (end %u, %u damaged segment(s)); connection closed",
                 unsigned(result.visit.end), unsigned(result.visit.skipped_segments));
        ex.close_connection();
        return ESP_FAIL;
    }
    std::vector<HistoryPoint> points;
    std::vector<HistoryGap> gap_records;
    bool truncated = false;
    if (!history_->query(query, points, gap_records, truncated))
        return send_error(ex, "503 Service Unavailable", "History unavailable or busy");
    char *text = build_history_json(points, std::move(gap_records), truncated, simulated_,
                                    query.boot_id.has_value());
    if (!text) {
        // No schema-valid answer could be built. The reply must not need the allocator that just
        // failed, so its body is a constant.
        ex.set_status("503 Service Unavailable");
        ex.set_type("application/json");
        ex.set_header("Cache-Control", "no-store");
        constexpr char body[] = "{\"error\":\"History response memory unavailable\"}";
        return ex.send(body, sizeof(body) - 1);
    }
    ex.set_type("application/json");
    ex.set_header("Cache-Control", "no-store");
    const auto sent = ex.send(text, std::strlen(text));
    cJSON_free(text);
    return sent;
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
            self.serve_history(*job->exchange, job->query, job->exporting) == ESP_OK;
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
    const bool portal_probe = path == "/generate_204" || path == "/gen_204" ||
                              path == "/hotspot-detect.html" || path == "/connecttest.txt" ||
                              path == "/ncsi.txt";
    if (portal_probe && ex.method() == HttpMethod::get && wifi_->info().provisioning) {
        // Only OS connectivity probes bypass Host checking; APIs never do.
        ex.set_status("302 Found");
        ex.set_header("Location", "http://192.168.4.1/setup");
        ex.set_header("Cache-Control", "no-store");
        return ex.send("", 0);
    }
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
        if (path == "/api/v1/clock" && ex.method() == HttpMethod::get) {
            const auto status = system_->clock_diagnostics();
            Json output(cJSON_CreateObject(), cJSON_Delete);
            cJSON_AddNumberToObject(output.get(), "schema_version", 1);
            cJSON_AddBoolToObject(output.get(), "synchronized", status.synchronized);
            if (status.last_sync_utc_seconds)
                cJSON_AddNumberToObject(output.get(), "last_sync_utc_seconds",
                                        *status.last_sync_utc_seconds);
            else
                cJSON_AddNullToObject(output.get(), "last_sync_utc_seconds");
            if (status.sync_age_ms)
                cJSON_AddNumberToObject(output.get(), "sync_age_ms", *status.sync_age_ms);
            else
                cJSON_AddNullToObject(output.get(), "sync_age_ms");
            return send_json(ex, output.get());
        }
        if (path == "/api/v1/probe-scan" && ex.method() == HttpMethod::get) {
            ProbeScanStatus status;
            LiveView view;
            if (!live_->read(view))
                return send_error(ex, "503 Service Unavailable", "Probe scan status busy");
            status = view.probe_scan;
            Json object(cJSON_CreateObject(), cJSON_Delete);
            cJSON_AddNumberToObject(object.get(), "schema_version", 1);
            cJSON_AddBoolToObject(object.get(), "enabled", status.enabled);
            cJSON_AddBoolToObject(object.get(), "complete", status.complete);
            cJSON_AddNumberToObject(object.get(), "discovered_count", status.discovered_count);
            cJSON_AddBoolToObject(object.get(), "overflow", status.overflow);
            return send_json(ex, object.get());
        }
        if (path == "/api/v1/ota" && ex.method() == HttpMethod::post)
            return system_->upload_firmware(ex);
        if (path == "/api/v1/system/reboot" && ex.method() == HttpMethod::post) {
            if (ex.content_length() != 0) {
                auto result =
                    send_error(ex, "400 Bad Request", "Reboot request must not include a body");
                ex.close_connection();
                return result;
            }
            if (!system_->reboot_available())
                return send_error(ex, "503 Service Unavailable", "Could not schedule restart");
            ex.set_status("202 Accepted");
            ex.set_type("application/json");
            ex.set_header("Cache-Control", "no-store");
            const auto result = send_text(ex, "{\"status\":\"rebooting\"}");
            if (!system_->schedule_reboot())
                system_->restart_now();
            return result;
        }
        if (path == "/api/v1/system/factory-reset" && ex.method() == HttpMethod::post) {
            auto input = read_json_body(ex);
            if (!input || cJSON_GetArraySize(input.get()) != 1)
                return send_error(ex, "400 Bad Request", "Expected erase_history boolean");
            auto *erase = cJSON_GetObjectItemCaseSensitive(input.get(), "erase_history");
            if (!cJSON_IsBool(erase))
                return send_error(ex, "400 Bad Request", "Expected erase_history boolean");
            if (!system_->schedule_factory_reset(cJSON_IsTrue(erase)))
                return send_error(ex, "503 Service Unavailable",
                                  "Could not schedule factory reset");
            ex.set_status("202 Accepted");
            ex.set_type("application/json");
            ex.set_header("Cache-Control", "no-store");
            return send_text(ex, "{\"status\":\"resetting\"}");
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
        if ((path == "/api/v1/history" || path == "/api/v1/history/export") &&
            ex.method() == HttpMethod::get) {
            const auto query_text = url_query(ex);
            if (query_text.empty() || query_text.size() > 256)
                return send_error(ex, "400 Bad Request", "Bounded history query required");
            bool has_max_points = false;
            auto parsed_query = parse_history_query(query_text, &has_max_points);
            if (!parsed_query)
                return send_error(ex, "400 Bad Request", "Invalid history bounds or selector");
            const auto &query = *parsed_query;
            const bool exporting = path == "/api/v1/history/export";
            if (exporting && has_max_points)
                return send_error(ex, "400 Bad Request",
                                  "CSV exports full five-minute records; omit max_points");
            // History is public, so it is rate limited per client (one query per second, one
            // export per 30 s, two at a time overall); a refused request says when to retry.
            const auto kind = exporting ? PublicRequest::export_csv : PublicRequest::history;
            const auto allowed = begin_public(kind, ex.client());
            if (!allowed.allow) {
                char retry[24];
                std::snprintf(retry, sizeof(retry), "%lld",
                              static_cast<long long>(
                                  std::max<int64_t>(1, (allowed.retry_after_ms + 999) / 1000)));
                ex.set_header("Retry-After", retry);
                return send_error(ex, "429 Too Many Requests",
                                  "Too many history requests; retry later");
            }
            // The query or export runs on the history worker, so this task keeps answering other
            // requests while it reads the store and sends the answer.
            auto *job = new (std::nothrow) HistoryJob{nullptr, query, exporting, kind};
            if (job)
                job->exchange = ex.begin_async();
            if (!job || !job->exchange) {
                delete job;
                finish_public(kind);
                return send_error(ex, "503 Service Unavailable", "History unavailable or busy");
            }
            if (!history_queue_ || xQueueSend(history_queue_, &job, 0) != pdTRUE) {
                delete job; // completes the async copy
                finish_public(kind);
                return send_error(ex, "503 Service Unavailable", "History unavailable or busy");
            }
            return ESP_OK; // the history worker answers on the async copy
        }
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
        if (path == "/api/v1/status" && ex.method() == HttpMethod::get) {
            auto text = snapshot();
            if (text.empty())
                return send_error(ex, "503 Service Unavailable", "Snapshot busy");
            ex.set_type("application/json");
            ex.set_header("Cache-Control", "no-store");
            return ex.send(text.data(), text.size());
        }
        if (path == "/api/v1/auth/logout" && ex.method() == HttpMethod::post)
            return route_auth_logout(request, *ports_);
        if (path == "/api/v1/auth/password" && ex.method() == HttpMethod::post)
            return route_auth_password(request, *ports_);
        return send_error(ex, "404 Not Found", "Unknown API route");
    }
    if (ex.method() != HttpMethod::get)
        return send_error(ex, "405 Method Not Allowed", "Method not allowed");
    const WebAsset *asset = nullptr;
    for (const auto &candidate : web_assets)
        if (path == candidate.path) {
            asset = &candidate;
            break;
        }
    if (!asset) {
        if (path != "/" && path != "/history" && path != "/sensors" && path != "/alerts" &&
            path != "/events" && path != "/settings" && path != "/setup")
            return send_error(ex, "404 Not Found", "Not found");
        for (const auto &candidate : web_assets)
            if (std::strcmp(candidate.path, "/index.html") == 0) {
                asset = &candidate;
                break;
            }
    }
    if (!asset)
        return ESP_FAIL;
    if (ex.header("Accept-Encoding", 256).find("gzip") == std::string::npos)
        return send_error(ex, "406 Not Acceptable", "Client must accept gzip");
    ex.set_type(asset->type);
    ex.set_header("Content-Encoding", "gzip");
    ex.set_header("Vary", "Accept-Encoding");
    ex.set_header("Cache-Control", std::strcmp(asset->path, "/index.html") == 0
                                       ? "no-cache"
                                       : "public, max-age=31536000, immutable");
    ex.set_header("X-Content-Type-Options", "nosniff");
    ex.set_header(
        "Content-Security-Policy",
        "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src "
        "'self' ws:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");
    const esp_err_t sent = ex.send(reinterpret_cast<const char *>(asset->data), asset->size);
    // The browser fetches the page's files in parallel over at most six connections, and each
    // keep-alive connection holds one of max_open_sockets. Closing after an asset frees its
    // socket at once, so the live WebSocket and API calls are not purged to make room.
    ex.close_connection();
    return sent;
}
} // namespace monitor
