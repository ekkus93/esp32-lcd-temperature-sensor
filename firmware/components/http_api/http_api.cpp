#include "http_api.hpp"
#include "api_responses.hpp"
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
#include "login_throttle.hpp"
#include "lwip/sockets.h"
#include "ota_service.hpp"
#include "public_limiter.hpp"
#include "retirement_policy.hpp"
#include "route_access.hpp"
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
const char *event_kind_name(EventKind kind) {
    switch (kind) {
    case EventKind::sensor_state:
        return "sensor_state";
    case EventKind::wifi_state:
        return "wifi_state";
    case EventKind::clock_sync:
        return "clock_sync";
    case EventKind::reboot:
        return "reboot";
    case EventKind::storage_error:
        return "storage_error";
    case EventKind::calibration_started:
        return "calibration_started";
    case EventKind::calibration_ended:
        return "calibration_ended";
    }
    return "sensor_state";
}
// Whether a probe is in an ice-bath calibration hold (spec 17.3), and until when. A hold ends at
// its sample cycle once its time is up, so presence here is the current state.
void add_calibration_hold(cJSON *item, const Sensor &sensor) {
    cJSON_AddBoolToObject(item, "calibrating", sensor.calibration_hold_until_ms.has_value());
    if (sensor.calibration_hold_until_ms)
        cJSON_AddNumberToObject(item, "calibration_hold_ends_uptime_ms",
                                static_cast<double>(*sensor.calibration_hold_until_ms));
    else
        cJSON_AddNullToObject(item, "calibration_hold_ends_uptime_ms");
}

std::string boot_text(uint64_t boot_id) {
    char value[17]{};
    std::snprintf(value, sizeof(value), "%016llx", static_cast<unsigned long long>(boot_id));
    return value;
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
    auto value = auth_->authorize(session_cookie(ex), now());
    if (!session_accepted(value.has_value(), csrf,
                          value && constant_equal(ex.header("X-CSRF-Token", 128), value->csrf)))
        return std::nullopt;
    return value;
}
void HttpApi::login_worker(void *context) {
    auto &self = *static_cast<HttpApi *>(context);
    for (;;) {
        LoginJob *job = nullptr;
        if (xQueueReceive(self.login_queue_, &job, portMAX_DELAY) != pdTRUE || !job)
            continue;
        auto failure = AuthService::LoginFailure::none;
        const auto session =
            self.auth_->login_complete(job->password, job->client, now(), &failure);
        std::fill(job->password.begin(), job->password.end(), '\0');
        auto &ex = *job->exchange;
        if (!session && failure == AuthService::LoginFailure::wrong_password) {
            ex.set_header("Retry-After", "5");
            send_error(ex, "401 Unauthorized", "Login denied; retry after 5 seconds");
        } else if (!session) {
            // Not the password's fault: say so, so nobody retypes a correct password.
            ex.set_header("Retry-After", "5");
            send_error(ex, "503 Service Unavailable",
                       "The device could not check the password just now; try again in a few "
                       "seconds");
        } else {
            ex.set_header("Set-Cookie", "tm_session=" + session->token +
                                            "; Path=/; HttpOnly; SameSite=Strict; Max-Age=3600");
            Json object(cJSON_CreateObject(), cJSON_Delete);
            cJSON_AddStringToObject(object.get(), "csrf", session->csrf.c_str());
            send_json(ex, object.get());
        }
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
    auth_ = &auth;
    wifi_ = &wifi;
    history_ = &history;
    settings_ = &settings;
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
    Registry registry;
    int64_t uptime;
    bool logging, synced;
    std::string utc;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE)
        return {};
    registry = registry_;
    uptime = uptime_;
    logging = logging_;
    synced = synchronized_;
    utc = utc_;
    xSemaphoreGive(mutex_);
    Json object(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddNumberToObject(object.get(), "version", 1);
    cJSON_AddNumberToObject(object.get(), "sequence", registry.sequence());
    cJSON_AddStringToObject(object.get(), "boot_id", boot_id_.c_str());
    cJSON_AddNumberToObject(object.get(), "uptime_ms", uptime);
    cJSON_AddBoolToObject(object.get(), "simulated", simulated_);
    cJSON_AddBoolToObject(object.get(), "bus_fault", registry.bus_fault());
    const auto update = ota_status();
    auto *firmware = cJSON_AddObjectToObject(object.get(), "firmware");
    cJSON_AddStringToObject(firmware, "version", update.version.c_str());
    cJSON_AddStringToObject(firmware, "partition", update.partition.c_str());
    cJSON_AddStringToObject(firmware, "ota_state", update.state.c_str());
    auto *clock = cJSON_AddObjectToObject(object.get(), "clock");
    cJSON_AddBoolToObject(clock, "synchronized", synced);
    if (synced)
        cJSON_AddStringToObject(clock, "utc", utc.c_str());
    else
        cJSON_AddNullToObject(clock, "utc");
    auto *wifi = cJSON_AddObjectToObject(object.get(), "wifi");
    auto network = wifi_->info();
    cJSON_AddStringToObject(wifi, "state", network.state.c_str());
    // The name mDNS is really using (it changes after a conflict); null when mDNS is unavailable.
    if (const auto name = wifi_->mdns_hostname(); !name.empty())
        cJSON_AddStringToObject(wifi, "mdns_hostname", name.c_str());
    else
        cJSON_AddNullToObject(wifi, "mdns_hostname");
    auto *history = cJSON_AddObjectToObject(object.get(), "logging");
    cJSON_AddBoolToObject(history, "healthy", logging);
    cJSON_AddNumberToObject(history, "interval_seconds", 300);
    auto storage = history_->storage_info();
    cJSON_AddBoolToObject(history, "storage_available", storage.available);
    cJSON_AddNumberToObject(history, "storage_total_bytes", storage.total_bytes);
    cJSON_AddNumberToObject(history, "storage_used_bytes", storage.used_bytes);
    auto *sensors = cJSON_AddArrayToObject(object.get(), "sensors");
    auto ordered = registry.sensors();
    std::stable_sort(ordered.begin(), ordered.end(), [](const Sensor &a, const Sensor &b) {
        return std::tie(a.settings.order, a.rom) < std::tie(b.settings.order, b.rom);
    });
    for (const auto &sensor : ordered) {
        auto *s = cJSON_CreateObject();
        cJSON_AddItemToArray(sensors, s);
        cJSON_AddStringToObject(s, "rom_id", rom_hex(sensor.rom).c_str());
        cJSON_AddStringToObject(s, "name", sensor.settings.name.c_str());
        cJSON_AddStringToObject(s, "state", state_name(sensor.state));
        if (sensor.temperature)
            cJSON_AddNumberToObject(s, "temperature_c", *sensor.temperature);
        else
            cJSON_AddNullToObject(s, "temperature_c");
        if (sensor.last_seen_ms)
            cJSON_AddNumberToObject(s, "last_seen_ms", *sensor.last_seen_ms);
        else
            cJSON_AddNullToObject(s, "last_seen_ms");
        cJSON_AddBoolToObject(s, "enabled", sensor.settings.enabled);
        cJSON_AddNumberToObject(s, "order", sensor.settings.order);
        add_calibration_hold(s, sensor);
    }
    char *text = cJSON_PrintUnformatted(object.get());
    if (!text)
        return {};
    std::string result(text);
    cJSON_free(text);
    return result;
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
    if (path == "/api/v1/auth/state" && ex.method() == HttpMethod::get) {
        Json object(cJSON_CreateObject(), cJSON_Delete);
        cJSON_AddBoolToObject(object.get(), "enrolled", auth_->enrolled());
        const auto signed_in = session(ex, false);
        cJSON_AddBoolToObject(object.get(), "signed_in", signed_in.has_value());
        if (signed_in)
            cJSON_AddStringToObject(object.get(), "csrf", signed_in->csrf.c_str());
        return send_json(ex, object.get());
    }
    if ((path == "/api/v1/auth/enroll" || path == "/api/v1/auth/login") &&
        ex.method() == HttpMethod::post) {
        auto input = read_json_body(ex);
        if (!input)
            return send_error(ex, "400 Bad Request", "Expected bounded JSON object");
        if (path == "/api/v1/auth/enroll") {
            const auto result = auth_->enroll(json_string(input.get(), "password"), now(),
                                              wifi_->info().provisioning);
            const auto reply = enrollment_reply(result);
            if (result != AuthService::EnrollResult::enrolled) {
                char retry[12];
                if (reply.retry_after > 0) {
                    std::snprintf(retry, sizeof(retry), "%d", reply.retry_after);
                    ex.set_header("Retry-After", retry);
                }
                return send_error(ex, reply.status, reply.message);
            }
            ex.set_status(reply.status);
            return ex.send("{}", 2);
        }
        // Login: the slow password check runs on a worker so other requests keep being served.
        std::string password = json_string(input.get(), "password");
        const uint32_t client = ex.client();
        const auto decision = auth_->login_begin(client, now());
        if (decision.gate != LoginGate::allow) {
            char retry[24];
            std::snprintf(retry, sizeof(retry), "%lld",
                          static_cast<long long>(
                              std::max<int64_t>(1, (decision.retry_after_ms + 999) / 1000)));
            ex.set_header("Retry-After", retry);
            return send_error(ex, "429 Too Many Requests",
                              decision.gate == LoginGate::busy
                                  ? "Another login is being checked; retry shortly"
                                  : "Too many failed logins; retry later");
        }
        auto *job = new (std::nothrow) LoginJob{nullptr, std::move(password), client};
        if (job)
            job->exchange = ex.begin_async();
        if (!job || !job->exchange) {
            delete job;
            auth_->login_abort(client);
            return send_error(ex, "503 Service Unavailable", "Login unavailable; retry shortly");
        }
        if (!login_queue_ || xQueueSend(login_queue_, &job, 0) != pdTRUE) {
            delete job; // completes the async copy
            auth_->login_abort(client);
            return send_error(ex, "503 Service Unavailable", "Login unavailable; retry shortly");
        }
        return ESP_OK; // the worker answers on the async copy
    }
    if (api) {
        // One place decides who may call what (route_access.hpp). A route that is not in the
        // table is a 404 for everyone, so no handler below is reachable by accident.
        const char *method_name = ex.method() == HttpMethod::get     ? "GET"
                                  : ex.method() == HttpMethod::post  ? "POST"
                                  : ex.method() == HttpMethod::patch ? "PATCH"
                                  : ex.method() == HttpMethod::put   ? "PUT"
                                  : ex.method() == HttpMethod::del   ? "DELETE"
                                                                     : "";
        const auto access = route_access(method_name, path);
        // Owner routes need a session (and, for changes, the CSRF token). Open routes work
        // without one; a signed-in owner is still recognised so owner-only fields can be added.
        const auto s = session(ex, route_needs_csrf(access, method_name));
        switch (api_gate(access, s.has_value())) {
        case ApiGate::not_found:
            return send_error(ex, "404 Not Found", "Unknown API route");
        case ApiGate::unauthorized:
            return send_error(ex, "401 Unauthorized", "Administrator session required");
        case ApiGate::proceed:
            break;
        }
        if (path == "/api/v1/clock" && ex.method() == HttpMethod::get) {
            const auto status = clock_sync_diagnostics();
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
            if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE)
                return send_error(ex, "503 Service Unavailable", "Probe scan status busy");
            status = probe_scan_;
            xSemaphoreGive(mutex_);
            Json object(cJSON_CreateObject(), cJSON_Delete);
            cJSON_AddNumberToObject(object.get(), "schema_version", 1);
            cJSON_AddBoolToObject(object.get(), "enabled", status.enabled);
            cJSON_AddBoolToObject(object.get(), "complete", status.complete);
            cJSON_AddNumberToObject(object.get(), "discovered_count", status.discovered_count);
            cJSON_AddBoolToObject(object.get(), "overflow", status.overflow);
            return send_json(ex, object.get());
        }
        if (path == "/api/v1/ota" && ex.method() == HttpMethod::post)
            return handle_ota_upload(static_cast<httpd_req_t *>(ex.native_handle()));
        if (path == "/api/v1/system/reboot" && ex.method() == HttpMethod::post) {
            if (ex.content_length() != 0) {
                auto result =
                    send_error(ex, "400 Bad Request", "Reboot request must not include a body");
                ex.close_connection();
                return result;
            }
            if (!reboot_available())
                return send_error(ex, "503 Service Unavailable", "Could not schedule restart");
            ex.set_status("202 Accepted");
            ex.set_type("application/json");
            ex.set_header("Cache-Control", "no-store");
            const auto result = send_text(ex, "{\"status\":\"rebooting\"}");
            if (!schedule_reboot())
                esp_restart();
            return result;
        }
        if (path == "/api/v1/system/factory-reset" && ex.method() == HttpMethod::post) {
            auto input = read_json_body(ex);
            if (!input || cJSON_GetArraySize(input.get()) != 1)
                return send_error(ex, "400 Bad Request", "Expected erase_history boolean");
            auto *erase = cJSON_GetObjectItemCaseSensitive(input.get(), "erase_history");
            if (!cJSON_IsBool(erase))
                return send_error(ex, "400 Bad Request", "Expected erase_history boolean");
            if (!schedule_factory_reset(cJSON_IsTrue(erase)))
                return send_error(ex, "503 Service Unavailable",
                                  "Could not schedule factory reset");
            ex.set_status("202 Accepted");
            ex.set_type("application/json");
            ex.set_header("Cache-Control", "no-store");
            return send_text(ex, "{\"status\":\"resetting\"}");
        }
        if (path == "/api/v1/settings" &&
            (ex.method() == HttpMethod::get || ex.method() == HttpMethod::patch)) {
            DeviceOptions options;
            uint64_t revision = 0;
            bool writable = false;
            if (!settings_->get(options, revision, writable))
                return send_error(ex, "503 Service Unavailable", "Settings busy");
            auto etag = [&]() {
                return "\"" + boot_id_ + ":settings:" + std::to_string(revision) + "\"";
            };
            if (ex.method() == HttpMethod::patch) {
                if (!writable)
                    return send_error(ex, "503 Service Unavailable",
                                      "Stored settings damaged; preserved for recovery");
                auto match = ex.header("If-Match", 128);
                if (match.empty())
                    return send_error(ex, "428 Precondition Required",
                                      "Refresh settings before saving");
                if (match != etag())
                    return send_error(ex, "412 Precondition Failed",
                                      "Settings changed; refresh before saving");
                auto input = read_json_body(ex);
                if (!input)
                    return send_error(ex, "400 Bad Request", "Expected bounded JSON object");
                std::vector<std::string> keys;
                cJSON *field = nullptr;
                bool valid = true;
                cJSON_ArrayForEach(field, input.get()) {
                    std::string key = field->string;
                    if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
                        valid = false;
                        break;
                    }
                    keys.push_back(key);
                    if (key == "units" || key == "display_mode" || key == "timezone" ||
                        key == "ntp_server" || key == "hostname") {
                        if (!cJSON_IsString(field)) {
                            valid = false;
                            break;
                        }
                        std::string value = field->valuestring;
                        if (key == "units") {
                            if (value != "C" && value != "F") {
                                valid = false;
                                break;
                            }
                            options.fahrenheit = value == "F";
                        } else if (key == "display_mode") {
                            if (value != "basic" && value != "extended") {
                                valid = false;
                                break;
                            }
                            options.extended = value == "extended";
                        } else if (key == "timezone")
                            options.timezone = value;
                        else if (key == "hostname")
                            options.hostname = value;
                        else
                            options.ntp_server = value;
                    } else if (key == "overview_seconds" || key == "trends_seconds" ||
                               key == "lookback_minutes") {
                        if (!cJSON_IsNumber(field) || !std::isfinite(field->valuedouble) ||
                            field->valuedouble < 0 || field->valuedouble > 1440 ||
                            std::floor(field->valuedouble) != field->valuedouble) {
                            valid = false;
                            break;
                        }
                        const auto value = unsigned(field->valuedouble);
                        if (key == "overview_seconds")
                            options.overview_seconds = value;
                        else if (key == "trends_seconds")
                            options.trends_seconds = value;
                        else
                            options.lookback_minutes = value;
                    } else {
                        valid = false;
                        break;
                    }
                }
                // The schema requires at least one property (minProperties: 1).
                if (!valid || keys.empty() || !valid_device_options(options))
                    return send_error(ex, "400 Bad Request", "Invalid device settings");
                if (settings_->save(options, revision) != ESP_OK)
                    return send_error(ex, "503 Service Unavailable",
                                      "Settings could not be committed");
                apply_hostname(options.hostname);
                if (!configure_clock(options))
                    return send_error(ex, "503 Service Unavailable",
                                      "Settings saved; clock configuration unavailable");
                if (!settings_->get(options, revision, writable))
                    return send_error(ex, "503 Service Unavailable",
                                      "Settings saved; refresh to confirm");
            }
            Json output(cJSON_CreateObject(), cJSON_Delete);
            cJSON_AddNumberToObject(output.get(), "schema_version", 1);
            // Owner-only fields (etag, revision, writable, ntp_server) are left out of the public
            // response (device-settings-public.schema.json); a signed-in owner gets them all.
            if (s) {
                cJSON_AddStringToObject(output.get(), "etag", etag().c_str());
                cJSON_AddNumberToObject(output.get(), "revision", revision);
                cJSON_AddBoolToObject(output.get(), "writable", writable);
            }
            cJSON_AddStringToObject(output.get(), "units", options.fahrenheit ? "F" : "C");
            cJSON_AddStringToObject(output.get(), "display_mode",
                                    options.extended ? "extended" : "basic");
            cJSON_AddNumberToObject(output.get(), "overview_seconds", options.overview_seconds);
            cJSON_AddNumberToObject(output.get(), "trends_seconds", options.trends_seconds);
            cJSON_AddNumberToObject(output.get(), "lookback_minutes", options.lookback_minutes);
            cJSON_AddStringToObject(output.get(), "timezone", options.timezone.c_str());
            cJSON_AddStringToObject(output.get(), "hostname", options.hostname.c_str());
            // The NTP server is the owner's: anonymous readers get the display fields only.
            if (s)
                cJSON_AddStringToObject(output.get(), "ntp_server", options.ntp_server.c_str());
            return send_json(ex, output.get());
        }
        if (path == "/api/v1/wifi/scan" && ex.method() == HttpMethod::post) {
            if (!wifi_->scan())
                return send_error(ex, "429 Too Many Requests",
                                  "Scan busy; wait at least 10 seconds");
            ex.set_status("202 Accepted");
            return ex.send("{}", 2);
        }
        if (path == "/api/v1/wifi/scan" && ex.method() == HttpMethod::get) {
            std::vector<ScannedNetwork> networks;
            bool scanning = false;
            if (!wifi_->scan_results(networks, scanning))
                return send_error(ex, "503 Service Unavailable", "Scan results busy");
            Json output(cJSON_CreateObject(), cJSON_Delete);
            cJSON_AddBoolToObject(output.get(), "scanning", scanning);
            auto *items = cJSON_AddArrayToObject(output.get(), "networks");
            for (const auto &network : networks) {
                auto *item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "ssid", network.ssid.c_str());
                cJSON_AddNumberToObject(item, "rssi", network.rssi);
                cJSON_AddNumberToObject(item, "channel", network.channel);
                cJSON_AddBoolToObject(item, "secure", network.secure);
                cJSON_AddItemToArray(items, item);
            }
            return send_json(ex, output.get());
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
        if ((path == "/api/v1/sensors" || path == "/api/v1/alerts" ||
             path.rfind("/api/v1/sensors/", 0) == 0)) {
            Registry current;
            ProbeScanStatus scan;
            if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE)
                return send_error(ex, "503 Service Unavailable", "Snapshot busy");
            current = registry_;
            scan = probe_scan_;
            const uint64_t snapshot_generation = published_generation_.load();
            xSemaphoreGive(mutex_);
            if (path == "/api/v1/alerts" && ex.method() == HttpMethod::get) {
                uint64_t after = 0;
                unsigned limit = 50;
                const auto query_text = url_query(ex);
                if (query_text.size() > 64)
                    return send_error(ex, "400 Bad Request", "Invalid alert cursor");
                if (!query_text.empty()) {
                    bool valid = true;
                    std::vector<std::string> keys;
                    for (size_t start = 0; start < query_text.size();) {
                        auto end = query_text.find('&', start);
                        if (end == std::string::npos)
                            end = query_text.size();
                        const auto item = query_text.substr(start, end - start);
                        const auto equals = item.find('=');
                        const auto key = item.substr(0, equals);
                        const auto value =
                            equals == std::string::npos ? std::string{} : item.substr(equals + 1);
                        uint64_t number = 0;
                        const auto parsed =
                            std::from_chars(value.data(), value.data() + value.size(), number);
                        if (value.empty() ||
                            std::find(keys.begin(), keys.end(), key) != keys.end() ||
                            parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
                            valid = false;
                            break;
                        }
                        keys.push_back(key);
                        if (key == "after")
                            after = number;
                        else if (key == "limit" && number >= 1 && number <= 50)
                            limit = static_cast<unsigned>(number);
                        else
                            valid = false;
                        start = end + 1;
                    }
                    if (!valid)
                        return send_error(ex, "400 Bad Request", "Invalid alert cursor");
                }
                Json output(cJSON_CreateObject(), cJSON_Delete);
                auto *active = cJSON_AddArrayToObject(output.get(), "active");
                // When each condition began: uptime ms, and UTC seconds once the clock is trusted
                // (computed now, so a condition that began before the clock synced still gets one).
                const int64_t uptime_now = now();
                const auto utc_now = monitor::trusted_utc();
                auto add_since = [&](cJSON *event, int64_t since_ms) {
                    const int64_t since = std::clamp<int64_t>(since_ms, 0, uptime_now);
                    cJSON_AddNumberToObject(event, "since_uptime_ms", static_cast<double>(since));
                    if (utc_now)
                        cJSON_AddNumberToObject(
                            event, "since_utc_seconds",
                            static_cast<double>(*utc_now - (uptime_now - since) / 1000));
                    else
                        cJSON_AddNullToObject(event, "since_utc_seconds");
                };
                if (current.bus_fault()) {
                    auto *event = cJSON_CreateObject();
                    cJSON_AddStringToObject(event, "state", "bus_fault");
                    add_since(event, current.bus_fault_since().value_or(uptime_now));
                    cJSON_AddItemToArray(active, event);
                }
                // Losing the network is a condition that needs attention until it recovers. The
                // connect and disconnect transitions themselves are journal events.
                const auto network = wifi_->info();
                if (network.state == "offline") {
                    auto *event = cJSON_CreateObject();
                    cJSON_AddStringToObject(event, "state", "wifi_disconnected");
                    add_since(event, network.state_since_ms);
                    cJSON_AddItemToArray(active, event);
                }
                for (const auto &sensor : current.sensors())
                    if (sensor.state == State::high || sensor.state == State::low ||
                        (sensor.state == State::disconnected && sensor.settings.disconnect_alert)) {
                        auto *event = cJSON_CreateObject();
                        cJSON_AddStringToObject(event, "rom_id", rom_hex(sensor.rom).c_str());
                        cJSON_AddStringToObject(event, "name", sensor.settings.name.c_str());
                        cJSON_AddStringToObject(event, "state", state_name(sensor.state));
                        add_since(event, sensor.state_since_ms.value_or(0));
                        cJSON_AddItemToArray(active, event);
                    }
                auto *events = cJSON_AddArrayToObject(output.get(), "events");
                const auto page = history_->event_page(after, limit);
                cJSON_AddBoolToObject(output.get(), "event_log_healthy", page.available);
                if (page.oldest_available)
                    cJSON_AddNumberToObject(output.get(), "oldest_available",
                                            static_cast<double>(page.oldest_available));
                else
                    cJSON_AddNullToObject(output.get(), "oldest_available");
                for (const auto &event : page.events) {
                    auto *item = cJSON_CreateObject();
                    cJSON_AddNumberToObject(item, "sequence", event.sequence);
                    const auto boot = boot_text(event.boot_id);
                    cJSON_AddStringToObject(item, "boot_id", boot.c_str());
                    cJSON_AddStringToObject(item, "kind", event_kind_name(event.kind));
                    if (event.rom)
                        cJSON_AddStringToObject(item, "rom_id", rom_hex(event.rom).c_str());
                    else
                        cJSON_AddNullToObject(item, "rom_id");
                    cJSON_AddStringToObject(item, "previous", state_name(event.previous));
                    cJSON_AddStringToObject(item, "state", state_name(event.state));
                    cJSON_AddNumberToObject(item, "uptime_ms", event.uptime_ms);
                    if (event.utc_seconds)
                        cJSON_AddNumberToObject(item, "utc_seconds", *event.utc_seconds);
                    else
                        cJSON_AddNullToObject(item, "utc_seconds");
                    cJSON_AddNumberToObject(item, "detail", event.detail);
                    cJSON_AddItemToArray(events, item);
                }
                cJSON_AddNumberToObject(output.get(), "next_after",
                                        static_cast<double>(page.next_after));
                cJSON_AddBoolToObject(output.get(), "has_more", page.has_more);
                return send_json(ex, output.get());
            }
            auto serialize = [this](const Sensor &sensor) -> cJSON * {
                auto *item = cJSON_CreateObject();
                const auto &settings = sensor.settings;
                cJSON_AddStringToObject(item, "rom_id", rom_hex(sensor.rom).c_str());
                const std::string etag = "\"" + boot_id_ + ":" + rom_hex(sensor.rom) + ":" +
                                         std::to_string(sensor.config_revision) + "\"";
                cJSON_AddStringToObject(item, "etag", etag.c_str());
                cJSON_AddNumberToObject(item, "revision", sensor.config_revision);
                cJSON_AddStringToObject(item, "name", settings.name.c_str());
                cJSON_AddStringToObject(item, "short_name", settings.short_name.c_str());
                cJSON_AddBoolToObject(item, "enabled", settings.enabled);
                cJSON_AddBoolToObject(item, "disconnect_alert", settings.disconnect_alert);
                cJSON_AddNumberToObject(item, "order", settings.order);
                cJSON_AddNumberToObject(item, "calibration_c", settings.calibration);
                cJSON_AddNumberToObject(item, "hysteresis_c", settings.hysteresis);
                cJSON_AddNumberToObject(item, "persistence_seconds",
                                        settings.persistence_ms / 1000.0);
                if (settings.low)
                    cJSON_AddNumberToObject(item, "low_c", *settings.low);
                else
                    cJSON_AddNullToObject(item, "low_c");
                if (settings.high)
                    cJSON_AddNumberToObject(item, "high_c", *settings.high);
                else
                    cJSON_AddNullToObject(item, "high_c");
                cJSON_AddStringToObject(item, "state", state_name(sensor.state));
                cJSON_AddStringToObject(item, "probe_calibration",
                                        probe_calibration_name(sensor.probe_calibration));
                add_calibration_hold(item, sensor);
                return item;
            };
            if (path == "/api/v1/sensors" && ex.method() == HttpMethod::get) {
                Json output(cJSON_CreateArray(), cJSON_Delete);
                for (const auto &sensor : current.sensors())
                    cJSON_AddItemToArray(output.get(), serialize(sensor));
                return send_json(ex, output.get());
            }
            if (path == "/api/v1/sensors/storage" && ex.method() == HttpMethod::get) {
                Json output(cJSON_CreateObject(), cJSON_Delete);
                cJSON_AddNumberToObject(output.get(), "schema_version", 1);
                const bool writable = sensor_settings_writable_.load();
                cJSON_AddStringToObject(output.get(), "state", writable ? "ready" : "read_only");
                cJSON_AddBoolToObject(output.get(), "writable", writable);
                return send_json(ex, output.get());
            }
            constexpr std::string_view hold_suffix = "/calibration-hold";
            if (path.rfind("/api/v1/sensors/", 0) == 0 && path.size() > 16 + hold_suffix.size() &&
                path.substr(path.size() - hold_suffix.size()) == hold_suffix) {
                // Ice-bath calibration hold (spec 17.3). It is not a setting: no ETag, and it is
                // kept in RAM, so it does not touch sensor storage.
                const auto id = path.substr(16, path.size() - 16 - hold_suffix.size());
                auto found =
                    std::find_if(current.sensors().begin(), current.sensors().end(),
                                 [&](const Sensor &sensor) { return rom_hex(sensor.rom) == id; });
                if (found == current.sensors().end())
                    return send_error(ex, "404 Not Found", "Unknown ROM ID");
                CalibrationHoldUpdate update{found->rom, 0, ex.method() == HttpMethod::del};
                if (ex.method() == HttpMethod::put) {
                    constexpr int64_t minute_ms = 60000;
                    if (!probe_can_be_held(found->state))
                        return send_error(ex, "409 Conflict",
                                          "Probe must be connected to calibrate it");
                    auto input = read_json_body(ex);
                    if (!input)
                        return send_error(ex, "400 Bad Request", "Expected bounded JSON object");
                    const auto minutes = calibration_hold_minutes(input.get());
                    if (!minutes)
                        return send_error(ex, "400 Bad Request",
                                          "Hold for 1 to 20 minutes: {\"minutes\": n}");
                    update.duration_ms = int64_t(*minutes) * minute_ms;
                } else if (ex.method() == HttpMethod::del) {
                    if (ex.content_length() != 0)
                        return send_error(ex, "400 Bad Request",
                                          "Ending a hold does not accept a body");
                } else {
                    return send_error(ex, "405 Method Not Allowed", "Method not allowed");
                }
                auto *queued = new (std::nothrow) CalibrationHoldUpdate(update);
                if (!queued)
                    return send_error(ex, "503 Service Unavailable",
                                      "Calibration memory unavailable");
                if (xQueueSend(holds_, &queued, 0) != pdTRUE) {
                    delete queued;
                    return send_error(ex, "503 Service Unavailable",
                                      "Calibration request queue full");
                }
                ex.set_status("202 Accepted");
                return send_text(ex, "{\"status\":\"queued\"}");
            }
            if ((ex.method() == HttpMethod::patch || ex.method() == HttpMethod::del) &&
                path.rfind("/api/v1/sensors/", 0) == 0) {
                if (!sensor_settings_writable_.load())
                    return send_error(
                        ex, "503 Service Unavailable",
                        "Sensor settings are read-only; stored data is preserved for recovery");
                auto id = path.substr(16);
                auto found =
                    std::find_if(current.sensors().begin(), current.sensors().end(),
                                 [&](const Sensor &sensor) { return rom_hex(sensor.rom) == id; });
                if (found == current.sensors().end())
                    return send_error(ex, "404 Not Found", "Unknown ROM ID");
                if (sent_generation_.load() != published_generation_.load() ||
                    snapshot_generation != published_generation_.load())
                    return send_error(ex, "409 Conflict",
                                      "Another sensor update is pending; wait for the next sample");
                const std::string expected = "\"" + boot_id_ + ":" + rom_hex(found->rom) + ":" +
                                             std::to_string(found->config_revision) + "\"";
                auto match = ex.header("If-Match", 128);
                if (match.empty())
                    return send_error(ex, "428 Precondition Required",
                                      "Refresh sensor settings before updating");
                if (match != expected)
                    return send_error(ex, "412 Precondition Failed",
                                      "Sensor settings changed; refresh before saving");
                if (ex.method() == HttpMethod::del) {
                    if (ex.content_length() != 0)
                        return send_error(ex, "400 Bad Request",
                                          "Retirement does not accept a body");
                    if (!can_retire_sensor(found->state, found->rom, scan, simulated_))
                        return send_error(
                            ex, "409 Conflict",
                            "Probe must be disconnected and absent from a complete scan");
                    const uint64_t generation = sent_generation_.load() + 1;
                    auto *update = new (std::nothrow)
                        SensorUpdate{found->rom, found->settings, generation, true};
                    if (!update)
                        return send_error(ex, "503 Service Unavailable",
                                          "Configuration memory unavailable");
                    if (xQueueSend(updates_, &update, 0) != pdTRUE) {
                        delete update;
                        return send_error(ex, "503 Service Unavailable",
                                          "Configuration queue full");
                    }
                    sent_generation_ = generation;
                    ex.set_status("202 Accepted");
                    return send_text(ex, "{\"status\":\"queued\"}");
                }
                auto input = read_json_body(ex);
                if (!input)
                    return send_error(ex, "400 Bad Request", "Expected bounded JSON object");
                Settings settings = found->settings;
                bool valid = true;
                cJSON *field = nullptr;
                std::vector<std::string> keys;
                cJSON_ArrayForEach(field, input.get()) {
                    std::string key = field->string;
                    if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
                        valid = false;
                        break;
                    }
                    keys.push_back(key);
                    if (key == "name" || key == "short_name") {
                        if (!cJSON_IsString(field)) {
                            valid = false;
                            break;
                        }
                        if (key == "name")
                            settings.name = field->valuestring;
                        else
                            settings.short_name = field->valuestring;
                    } else if (key == "enabled" || key == "disconnect_alert") {
                        if (!cJSON_IsBool(field)) {
                            valid = false;
                            break;
                        }
                        if (key == "enabled")
                            settings.enabled = cJSON_IsTrue(field);
                        else
                            settings.disconnect_alert = cJSON_IsTrue(field);
                    } else if (key == "low_c" || key == "high_c") {
                        std::optional<double> value;
                        if (cJSON_IsNumber(field))
                            value = field->valuedouble;
                        else if (!cJSON_IsNull(field)) {
                            valid = false;
                            break;
                        }
                        if (key == "low_c")
                            settings.low = value;
                        else
                            settings.high = value;
                    } else if (key == "order" || key == "calibration_c" || key == "hysteresis_c" ||
                               key == "persistence_seconds") {
                        if (!cJSON_IsNumber(field) || !std::isfinite(field->valuedouble)) {
                            valid = false;
                            break;
                        }
                        double value = field->valuedouble;
                        if (key == "order") {
                            if (value < 0 || value > 4 || std::floor(value) != value) {
                                valid = false;
                                break;
                            }
                            settings.order = unsigned(value);
                        } else if (key == "calibration_c")
                            settings.calibration = value;
                        else if (key == "hysteresis_c")
                            settings.hysteresis = value;
                        else {
                            if (value < 0 || value > 3600) {
                                valid = false;
                                break;
                            }
                            settings.persistence_ms = int64_t(std::round(value * 1000));
                        }
                    } else {
                        valid = false;
                        break;
                    }
                }
                if (!valid || keys.empty() || !valid_settings(settings))
                    return send_error(ex, "400 Bad Request", "Invalid sensor settings");
                const uint64_t generation = sent_generation_.load() + 1;
                auto *update =
                    new (std::nothrow) SensorUpdate{found->rom, settings, generation, false};
                if (!update)
                    return send_error(ex, "503 Service Unavailable",
                                      "Configuration memory unavailable");
                if (xQueueSend(updates_, &update, 0) != pdTRUE) {
                    delete update;
                    return send_error(ex, "503 Service Unavailable", "Configuration queue full");
                }
                sent_generation_ = generation;
                ex.set_status("202 Accepted");
                return send_text(ex, "{\"status\":\"queued\"}");
            }
        }
        if (path == "/api/v1/status" && ex.method() == HttpMethod::get) {
            auto text = snapshot();
            if (text.empty())
                return send_error(ex, "503 Service Unavailable", "Snapshot busy");
            ex.set_type("application/json");
            ex.set_header("Cache-Control", "no-store");
            return ex.send(text.data(), text.size());
        }
        if (path == "/api/v1/wifi/provisioning" && ex.method() == HttpMethod::post) {
            if (!wifi_->provision())
                return send_error(ex, "503 Service Unavailable", "Network control busy");
            ex.set_status("202 Accepted");
            return ex.send("{}", 2);
        }
        if (path == "/api/v1/wifi/connect" && ex.method() == HttpMethod::post) {
            auto input = read_json_body(ex);
            if (input && wifi_->candidate_busy())
                return send_error(ex, "409 Conflict",
                                  "A Wi-Fi connection attempt is already in progress");
            if (!input || !wifi_->connect(json_string(input.get(), "ssid"),
                                          json_string(input.get(), "password")))
                return send_error(ex, "400 Bad Request",
                                  "Invalid network settings or control busy");
            ex.set_status("202 Accepted");
            return ex.send("{}", 2);
        }
        if (path == "/api/v1/wifi/status" && ex.method() == HttpMethod::get) {
            auto info = wifi_->info();
            Json object(cJSON_CreateObject(), cJSON_Delete);
            cJSON_AddStringToObject(object.get(), "state", info.state.c_str());
            cJSON_AddStringToObject(object.get(), "ip", info.ip.c_str());
            cJSON_AddBoolToObject(object.get(), "provisioning", info.provisioning);
            cJSON_AddBoolToObject(object.get(), "saved_credentials", info.saved_credentials);
            cJSON_AddStringToObject(object.get(), "attempt", info.attempt.c_str());
            cJSON_AddNumberToObject(object.get(), "remaining_seconds", info.remaining_seconds);
            cJSON_AddStringToObject(object.get(), "error", info.error.c_str());
            return send_json(ex, object.get());
        }
        if (path == "/api/v1/auth/logout" && ex.method() == HttpMethod::post) {
            auth_->logout(s->token);
            ex.set_header("Set-Cookie",
                          "tm_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
            return ex.send("{}", 2);
        }
        if (path == "/api/v1/auth/password" && ex.method() == HttpMethod::post) {
            auto input = read_json_body(ex);
            if (!input ||
                !auth_->change_password(s->token, json_string(input.get(), "old_password"),
                                        json_string(input.get(), "new_password"), now()))
                return send_error(ex, "403 Forbidden", "Password change denied");
            return ex.send("{}", 2);
        }
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
