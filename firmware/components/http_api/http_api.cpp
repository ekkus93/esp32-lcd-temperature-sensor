#include "http_api.hpp"
#include "cJSON.h"
#include "clock_service.hpp"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "history_csv.hpp"
#include "ota_service.hpp"
#include "web_assets.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
namespace monitor {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
int64_t now() {
    return esp_timer_get_time() / 1000;
}
std::string header(httpd_req_t *r, const char *name, size_t limit = 512) {
    auto size = httpd_req_get_hdr_value_len(r, name);
    if (!size || size > limit)
        return {};
    std::vector<char> text(size + 1);
    if (httpd_req_get_hdr_value_str(r, name, text.data(), text.size()) != ESP_OK)
        return {};
    return text.data();
}
esp_err_t error(httpd_req_t *r, const char *status, const char *message) {
    httpd_resp_set_status(r, status);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    Json object(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddStringToObject(object.get(), "error", message);
    char *text = cJSON_PrintUnformatted(object.get());
    if (!text)
        return ESP_ERR_NO_MEM;
    auto result = httpd_resp_send(r, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return result;
}
esp_err_t json(httpd_req_t *r, cJSON *object) {
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    char *text = cJSON_PrintUnformatted(object);
    if (!text)
        return ESP_ERR_NO_MEM;
    auto result = httpd_resp_send(r, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return result;
}
Json body(httpd_req_t *r) {
    if (r->content_len <= 0 || r->content_len > 2048 ||
        header(r, "Content-Type", 128).find("application/json") != 0)
        return {nullptr, cJSON_Delete};
    std::string text(r->content_len, '\0');
    size_t used = 0;
    while (used < text.size()) {
        int count = httpd_req_recv(r, text.data() + used, text.size() - used);
        if (count <= 0)
            return {nullptr, cJSON_Delete};
        used += count;
    }
    if (text.find('\0') != std::string::npos || text.find("\\u0000") != std::string::npos)
        return {nullptr, cJSON_Delete};
    Json value(cJSON_ParseWithLengthOpts(text.c_str(), text.size() + 1, nullptr, true),
               cJSON_Delete);
    std::fill(text.begin(), text.end(), 0);
    if (!value || !cJSON_IsObject(value.get()))
        return {nullptr, cJSON_Delete};
    return value;
}
std::string string(cJSON *object, const char *key) {
    auto *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : "";
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
    }
    return "sensor_state";
}
std::string boot_text(uint64_t boot_id) {
    char value[17]{};
    std::snprintf(value, sizeof(value), "%016llx", static_cast<unsigned long long>(boot_id));
    return value;
}
std::string cookie(httpd_req_t *r) {
    auto text = header(r, "Cookie");
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find(';', start);
        auto part = text.substr(start, end == std::string::npos ? end : end - start);
        while (!part.empty() && part.front() == ' ')
            part.erase(part.begin());
        if (part.rfind("tm_session=", 0) == 0)
            return part.substr(11);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return {};
}
} // namespace
bool HttpApi::origin(httpd_req_t *r, bool required) {
    auto host = header(r, "Host", 128);
    auto address = wifi_->info().ip;
    bool allowed =
        host == "192.168.4.1" || host == "192.168.4.1:80" || host == "temperature-monitor.local" ||
        host == "temperature-monitor.local:80" ||
        (!address.empty() && address != "0.0.0.0" && (host == address || host == address + ":80"));
    if (!allowed)
        return false;
    auto source = header(r, "Origin", 256);
    return source.empty() ? !required : source == "http://" + host;
}
Session *HttpApi::session(httpd_req_t *r, bool csrf) {
    auto *value = auth_->authorize(cookie(r), now());
    if (value && csrf && !constant_equal(header(r, "X-CSRF-Token", 128), value->csrf))
        return nullptr;
    return value;
}
bool HttpApi::start(AuthService &auth, WifiService &wifi, HistoryService &history,
                    DeviceSettingsService &settings, uint64_t id, bool simulated) {
    simulated_ = simulated;
    auth_ = &auth;
    wifi_ = &wifi;
    history_ = &history;
    settings_ = &settings;
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(id));
    boot_id_ = text;
    mutex_ = xSemaphoreCreateMutex();
    updates_ = xQueueCreate(4, sizeof(SensorUpdate *));
    if (!mutex_ || !updates_)
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
    for (auto method : {HTTP_GET, HTTP_POST, HTTP_PATCH}) {
        httpd_uri_t route{};
        route.uri = "/*";
        route.method = method;
        route.handler = dispatch;
        route.user_ctx = this;
        httpd_register_uri_handler(server_, &route);
    }
    return true;
}
bool HttpApi::next_sensor_update(uint64_t &rom, Settings &settings) {
    SensorUpdate *update = nullptr;
    if (!updates_ || xQueueReceive(updates_, &update, 0) != pdTRUE)
        return false;
    consumed_generation_ = update->generation;
    rom = update->rom;
    settings = update->settings;
    delete update;
    return true;
}
void HttpApi::publish(const Registry &registry, int64_t uptime, bool logging, bool synchronized,
                      const std::string &utc) {
    if (!server_)
        return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        registry_ = registry;
        uptime_ = uptime;
        logging_ = logging;
        synchronized_ = synchronized;
        utc_ = utc;
        published_generation_ = consumed_generation_;
        xSemaphoreGive(mutex_);
    }
    ws_live_.schedule();
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
        return a.settings.order < b.settings.order;
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
    }
    char *text = cJSON_PrintUnformatted(object.get());
    if (!text)
        return {};
    std::string result(text);
    cJSON_free(text);
    return result;
}
esp_err_t HttpApi::dispatch(httpd_req_t *request) {
    return static_cast<HttpApi *>(request->user_ctx)->handle(request);
}
esp_err_t HttpApi::ws_pre(httpd_req_t *r) {
    auto &self = *static_cast<HttpApi *>(r->user_ctx);
    auto *session = self.session(r, false);
    if (!self.origin(r, true) || !session) {
        error(r, "403 Forbidden", "WebSocket authorization failed");
        return ESP_FAIL;
    }
    const int fd = httpd_req_to_sockfd(r);
    if (!self.ws_live_.add_client(fd, session->token)) {
        error(r, "503 Service Unavailable", "Socket limit");
        return ESP_FAIL;
    }
    self.ws_live_.schedule();
    return ESP_OK;
}
esp_err_t HttpApi::live(httpd_req_t *r) {
    return static_cast<HttpApi *>(r->user_ctx)->ws_live_.receive(r);
}
esp_err_t HttpApi::handle(httpd_req_t *r) {
    std::string path = r->uri;
    auto query = path.find('?');
    if (query != std::string::npos)
        path.resize(query);
    bool api = path.rfind("/api/", 0) == 0;
    const bool portal_probe = path == "/generate_204" || path == "/gen_204" ||
                              path == "/hotspot-detect.html" || path == "/connecttest.txt" ||
                              path == "/ncsi.txt";
    if (portal_probe && r->method == HTTP_GET && wifi_->info().provisioning) {
        // Only OS connectivity probes bypass Host checking; APIs never do.
        httpd_resp_set_status(r, "302 Found");
        httpd_resp_set_hdr(r, "Location", "http://192.168.4.1/setup");
        httpd_resp_set_hdr(r, "Cache-Control", "no-store");
        return httpd_resp_send(r, "", 0);
    }
    if (api && !origin(r, r->method != HTTP_GET))
        return error(r, "403 Forbidden", "Invalid host/origin");
    if (path == "/api/v1/auth/state" && r->method == HTTP_GET) {
        Json object(cJSON_CreateObject(), cJSON_Delete);
        cJSON_AddBoolToObject(object.get(), "enrolled", auth_->enrolled());
        if (auto *s = session(r, false))
            cJSON_AddStringToObject(object.get(), "csrf", s->csrf.c_str());
        return json(r, object.get());
    }
    if ((path == "/api/v1/auth/enroll" || path == "/api/v1/auth/login") && r->method == HTTP_POST) {
        auto input = body(r);
        if (!input)
            return error(r, "400 Bad Request", "Expected bounded JSON object");
        if (path == "/api/v1/auth/enroll") {
            if (!wifi_->info().provisioning ||
                !auth_->enroll(string(input.get(), "setup_secret"), string(input.get(), "password"),
                               now()))
                return error(r, "403 Forbidden", "Enrollment denied; retry after 5 seconds");
            httpd_resp_set_status(r, "201 Created");
            return httpd_resp_send(r, "{}", 2);
        }
        auto *s = auth_->login(string(input.get(), "password"), now());
        if (!s)
            return error(r, "401 Unauthorized", "Login denied; retry after 5 seconds");
        std::string value =
            "tm_session=" + s->token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=3600";
        httpd_resp_set_hdr(r, "Set-Cookie", value.c_str());
        Json object(cJSON_CreateObject(), cJSON_Delete);
        cJSON_AddStringToObject(object.get(), "csrf", s->csrf.c_str());
        return json(r, object.get());
    }
    if (api) {
        auto *s = session(r, r->method != HTTP_GET);
        if (!s)
            return error(r, "401 Unauthorized", "Administrator session required");
        if (path == "/api/v1/ota" && r->method == HTTP_POST)
            return handle_ota_upload(r);
        if (path == "/api/v1/system/reboot" && r->method == HTTP_POST) {
            if (r->content_len != 0) {
                auto result = error(r, "400 Bad Request", "Reboot request must not include a body");
                httpd_sess_trigger_close(server_, httpd_req_to_sockfd(r));
                return result;
            }
            if (!reboot_available())
                return error(r, "503 Service Unavailable", "Could not schedule restart");
            httpd_resp_set_status(r, "202 Accepted");
            httpd_resp_set_type(r, "application/json");
            httpd_resp_set_hdr(r, "Cache-Control", "no-store");
            const auto result =
                httpd_resp_send(r, "{\"status\":\"rebooting\"}", HTTPD_RESP_USE_STRLEN);
            if (!schedule_reboot())
                esp_restart();
            return result;
        }
        if (path == "/api/v1/settings" && (r->method == HTTP_GET || r->method == HTTP_PATCH)) {
            DeviceOptions options;
            uint64_t revision = 0;
            bool writable = false;
            if (!settings_->get(options, revision, writable))
                return error(r, "503 Service Unavailable", "Settings busy");
            auto etag = [&]() {
                return "\"" + boot_id_ + ":settings:" + std::to_string(revision) + "\"";
            };
            if (r->method == HTTP_PATCH) {
                if (!writable)
                    return error(r, "503 Service Unavailable",
                                 "Stored settings damaged; preserved for recovery");
                auto match = header(r, "If-Match", 128);
                if (match.empty())
                    return error(r, "428 Precondition Required", "Refresh settings before saving");
                if (match != etag())
                    return error(r, "412 Precondition Failed",
                                 "Settings changed; refresh before saving");
                auto input = body(r);
                if (!input)
                    return error(r, "400 Bad Request", "Expected bounded JSON object");
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
                        key == "ntp_server") {
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
                if (!valid || !valid_device_options(options))
                    return error(r, "400 Bad Request", "Invalid device settings");
                if (settings_->save(options, revision) != ESP_OK)
                    return error(r, "503 Service Unavailable", "Settings could not be committed");
                if (!configure_clock(options))
                    return error(r, "503 Service Unavailable",
                                 "Settings saved; clock configuration unavailable");
                if (!settings_->get(options, revision, writable))
                    return error(r, "503 Service Unavailable",
                                 "Settings saved; refresh to confirm");
            }
            Json output(cJSON_CreateObject(), cJSON_Delete);
            cJSON_AddNumberToObject(output.get(), "schema_version", 1);
            cJSON_AddStringToObject(output.get(), "etag", etag().c_str());
            cJSON_AddNumberToObject(output.get(), "revision", revision);
            cJSON_AddBoolToObject(output.get(), "writable", writable);
            cJSON_AddStringToObject(output.get(), "units", options.fahrenheit ? "F" : "C");
            cJSON_AddStringToObject(output.get(), "display_mode",
                                    options.extended ? "extended" : "basic");
            cJSON_AddNumberToObject(output.get(), "overview_seconds", options.overview_seconds);
            cJSON_AddNumberToObject(output.get(), "trends_seconds", options.trends_seconds);
            cJSON_AddNumberToObject(output.get(), "lookback_minutes", options.lookback_minutes);
            cJSON_AddStringToObject(output.get(), "timezone", options.timezone.c_str());
            cJSON_AddStringToObject(output.get(), "ntp_server", options.ntp_server.c_str());
            return json(r, output.get());
        }
        if (path == "/api/v1/wifi/scan" && r->method == HTTP_POST) {
            if (!wifi_->scan())
                return error(r, "429 Too Many Requests", "Scan busy; wait at least 10 seconds");
            httpd_resp_set_status(r, "202 Accepted");
            return httpd_resp_send(r, "{}", 2);
        }
        if (path == "/api/v1/wifi/scan" && r->method == HTTP_GET) {
            std::vector<ScannedNetwork> networks;
            bool scanning = false;
            if (!wifi_->scan_results(networks, scanning))
                return error(r, "503 Service Unavailable", "Scan results busy");
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
            return json(r, output.get());
        }
        if ((path == "/api/v1/history" || path == "/api/v1/history/export") &&
            r->method == HTTP_GET) {
            const auto length = httpd_req_get_url_query_len(r);
            if (!length || length > 256)
                return error(r, "400 Bad Request", "Bounded history query required");
            std::string query_text(length + 1, '\0');
            if (httpd_req_get_url_query_str(r, query_text.data(), query_text.size()) != ESP_OK)
                return error(r, "400 Bad Request", "Invalid history query");
            query_text.resize(length);
            bool has_max_points = false;
            auto parsed_query = parse_history_query(query_text, &has_max_points);
            if (!parsed_query)
                return error(r, "400 Bad Request", "Invalid history bounds or selector");
            const auto &query = *parsed_query;
            if (path == "/api/v1/history/export") {
                if (has_max_points)
                    return error(r, "400 Bad Request",
                                 "CSV exports full five-minute records; omit max_points");
                bool started = false;
                auto start = [&]() {
                    if (started)
                        return true;
                    started = true;
                    httpd_resp_set_type(r, "text/csv; charset=utf-8");
                    httpd_resp_set_hdr(r, "Content-Disposition",
                                       "attachment; filename=temperature-history.csv");
                    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
                    return httpd_resp_send_chunk(r, history_csv_header(), HTTPD_RESP_USE_STRLEN) ==
                           ESP_OK;
                };
                const bool ok = history_->visit(query, [&](const Bucket &bucket) {
                    if (!start())
                        return false;
                    for (const auto &sensor : bucket.sensors) {
                        if (!query.roms.empty() && std::find(query.roms.begin(), query.roms.end(),
                                                             sensor.rom) == query.roms.end())
                            continue;
                        const auto row = history_csv_row(bucket, sensor, simulated_);
                        if (httpd_resp_send_chunk(r, row.data(), row.size()) != ESP_OK)
                            return false;
                    }
                    return true;
                });
                if (!ok) {
                    if (!started)
                        return error(r, "503 Service Unavailable", "History unavailable or busy");
                    httpd_sess_trigger_close(server_, httpd_req_to_sockfd(r));
                    return ESP_FAIL;
                }
                if (!start())
                    return ESP_FAIL;
                return httpd_resp_send_chunk(r, nullptr, 0);
            }
            std::vector<HistoryPoint> points;
            bool truncated = false;
            if (!history_->query(query, points, truncated))
                return error(r, "503 Service Unavailable", "History unavailable or busy");
            Json output(cJSON_CreateObject(), cJSON_Delete);
            cJSON_AddNumberToObject(output.get(), "schema_version", 1);
            cJSON_AddBoolToObject(output.get(), "simulated", simulated_);
            cJSON_AddBoolToObject(output.get(), "truncated", truncated);
            cJSON_AddStringToObject(output.get(), "time_basis", query.boot_id ? "uptime" : "utc");
            auto *items = cJSON_AddArrayToObject(output.get(), "points");
            for (const auto &point : points) {
                auto *item = cJSON_CreateObject();
                char boot[17];
                std::snprintf(boot, sizeof(boot), "%016llx",
                              static_cast<unsigned long long>(point.boot_id));
                cJSON_AddStringToObject(item, "boot_id", boot);
                cJSON_AddStringToObject(item, "rom_id", rom_hex(point.rom).c_str());
                cJSON_AddNumberToObject(item, "start_ms", point.start_ms);
                cJSON_AddNumberToObject(item, "end_ms", point.end_ms);
                if (point.utc_seconds)
                    cJSON_AddNumberToObject(item, "utc_seconds", *point.utc_seconds);
                else
                    cJSON_AddNullToObject(item, "utc_seconds");
                cJSON_AddNumberToObject(item, "average_c", point.average);
                cJSON_AddNumberToObject(item, "minimum_c", point.minimum);
                cJSON_AddNumberToObject(item, "maximum_c", point.maximum);
                cJSON_AddNumberToObject(item, "count", point.count);
                cJSON_AddBoolToObject(item, "partial", point.partial);
                cJSON_AddItemToArray(items, item);
            }
            auto *gaps = cJSON_AddArrayToObject(output.get(), "gaps");
            for (const auto &gap : history_gaps(points)) {
                auto *item = cJSON_CreateObject();
                char boot[17];
                std::snprintf(boot, sizeof(boot), "%016llx",
                              static_cast<unsigned long long>(gap.boot_id));
                cJSON_AddStringToObject(item, "boot_id", boot);
                cJSON_AddStringToObject(item, "rom_id", rom_hex(gap.rom).c_str());
                cJSON_AddNumberToObject(item, "start_ms", gap.start_ms);
                cJSON_AddNumberToObject(item, "end_ms", gap.end_ms);
                cJSON_AddItemToArray(gaps, item);
            }
            return json(r, output.get());
        }
        if ((path == "/api/v1/sensors" || path == "/api/v1/alerts" ||
             path.rfind("/api/v1/sensors/", 0) == 0)) {
            Registry current;
            if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE)
                return error(r, "503 Service Unavailable", "Snapshot busy");
            current = registry_;
            const uint64_t snapshot_generation = published_generation_.load();
            xSemaphoreGive(mutex_);
            if (path == "/api/v1/alerts" && r->method == HTTP_GET) {
                uint64_t after = 0;
                unsigned limit = 50;
                const auto query_length = httpd_req_get_url_query_len(r);
                if (query_length > 64)
                    return error(r, "400 Bad Request", "Invalid alert cursor");
                if (query_length) {
                    std::string query_text(query_length + 1, '\0');
                    if (httpd_req_get_url_query_str(r, query_text.data(), query_text.size()) !=
                        ESP_OK)
                        return error(r, "400 Bad Request", "Invalid alert cursor");
                    query_text.resize(query_length);
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
                        return error(r, "400 Bad Request", "Invalid alert cursor");
                }
                Json output(cJSON_CreateObject(), cJSON_Delete);
                auto *active = cJSON_AddArrayToObject(output.get(), "active");
                if (current.bus_fault()) {
                    auto *event = cJSON_CreateObject();
                    cJSON_AddStringToObject(event, "state", "bus_fault");
                    cJSON_AddItemToArray(active, event);
                }
                for (const auto &sensor : current.sensors())
                    if (sensor.state == State::high || sensor.state == State::low ||
                        (sensor.state == State::disconnected && sensor.settings.disconnect_alert)) {
                        auto *event = cJSON_CreateObject();
                        cJSON_AddStringToObject(event, "rom_id", rom_hex(sensor.rom).c_str());
                        cJSON_AddStringToObject(event, "name", sensor.settings.name.c_str());
                        cJSON_AddStringToObject(event, "state", state_name(sensor.state));
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
                return json(r, output.get());
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
                return item;
            };
            if (path == "/api/v1/sensors" && r->method == HTTP_GET) {
                Json output(cJSON_CreateArray(), cJSON_Delete);
                for (const auto &sensor : current.sensors())
                    cJSON_AddItemToArray(output.get(), serialize(sensor));
                return json(r, output.get());
            }
            if (r->method == HTTP_PATCH && path.rfind("/api/v1/sensors/", 0) == 0) {
                auto id = path.substr(16);
                auto found =
                    std::find_if(current.sensors().begin(), current.sensors().end(),
                                 [&](const Sensor &sensor) { return rom_hex(sensor.rom) == id; });
                if (found == current.sensors().end())
                    return error(r, "404 Not Found", "Unknown ROM ID");
                if (sent_generation_.load() != published_generation_.load() ||
                    snapshot_generation != published_generation_.load())
                    return error(r, "409 Conflict",
                                 "Another sensor update is pending; wait for the next sample");
                const std::string expected = "\"" + boot_id_ + ":" + rom_hex(found->rom) + ":" +
                                             std::to_string(found->config_revision) + "\"";
                auto match = header(r, "If-Match", 128);
                if (match.empty())
                    return error(r, "428 Precondition Required",
                                 "Refresh sensor settings before updating");
                if (match != expected)
                    return error(r, "412 Precondition Failed",
                                 "Sensor settings changed; refresh before saving");
                auto input = body(r);
                if (!input)
                    return error(r, "400 Bad Request", "Expected bounded JSON object");
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
                if (!valid || !valid_settings(settings))
                    return error(r, "400 Bad Request", "Invalid sensor settings");
                const uint64_t generation = sent_generation_.load() + 1;
                auto *update = new (std::nothrow) SensorUpdate{found->rom, settings, generation};
                if (!update)
                    return error(r, "503 Service Unavailable", "Configuration memory unavailable");
                if (xQueueSend(updates_, &update, 0) != pdTRUE) {
                    delete update;
                    return error(r, "503 Service Unavailable", "Configuration queue full");
                }
                sent_generation_ = generation;
                httpd_resp_set_status(r, "202 Accepted");
                return httpd_resp_send(r, "{\"status\":\"queued\"}", HTTPD_RESP_USE_STRLEN);
            }
        }
        if (path == "/api/v1/status" && r->method == HTTP_GET) {
            auto text = snapshot();
            if (text.empty())
                return error(r, "503 Service Unavailable", "Snapshot busy");
            httpd_resp_set_type(r, "application/json");
            httpd_resp_set_hdr(r, "Cache-Control", "no-store");
            return httpd_resp_send(r, text.data(), text.size());
        }
        if (path == "/api/v1/wifi/provisioning" && r->method == HTTP_POST) {
            if (!wifi_->provision())
                return error(r, "503 Service Unavailable", "Network control busy");
            httpd_resp_set_status(r, "202 Accepted");
            return httpd_resp_send(r, "{}", 2);
        }
        if (path == "/api/v1/wifi/connect" && r->method == HTTP_POST) {
            auto input = body(r);
            if (input && wifi_->candidate_busy())
                return error(r, "409 Conflict",
                             "A Wi-Fi connection attempt is already in progress");
            if (!input ||
                !wifi_->connect(string(input.get(), "ssid"), string(input.get(), "password")))
                return error(r, "400 Bad Request", "Invalid network settings or control busy");
            httpd_resp_set_status(r, "202 Accepted");
            return httpd_resp_send(r, "{}", 2);
        }
        if (path == "/api/v1/wifi/status" && r->method == HTTP_GET) {
            auto info = wifi_->info();
            Json object(cJSON_CreateObject(), cJSON_Delete);
            cJSON_AddStringToObject(object.get(), "state", info.state.c_str());
            cJSON_AddStringToObject(object.get(), "ip", info.ip.c_str());
            cJSON_AddBoolToObject(object.get(), "provisioning", info.provisioning);
            cJSON_AddBoolToObject(object.get(), "saved_credentials", info.saved_credentials);
            cJSON_AddStringToObject(object.get(), "attempt", info.attempt.c_str());
            cJSON_AddNumberToObject(object.get(), "remaining_seconds", info.remaining_seconds);
            cJSON_AddStringToObject(object.get(), "error", info.error.c_str());
            return json(r, object.get());
        }
        if (path == "/api/v1/auth/logout" && r->method == HTTP_POST) {
            auth_->logout(s->token);
            httpd_resp_set_hdr(r, "Set-Cookie",
                               "tm_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
            return httpd_resp_send(r, "{}", 2);
        }
        if (path == "/api/v1/auth/password" && r->method == HTTP_POST) {
            auto input = body(r);
            if (!input || !auth_->change_password(s->token, string(input.get(), "old_password"),
                                                  string(input.get(), "new_password"), now()))
                return error(r, "403 Forbidden", "Password change denied");
            return httpd_resp_send(r, "{}", 2);
        }
        return error(r, "404 Not Found", "Unknown API route");
    }
    if (r->method != HTTP_GET)
        return error(r, "405 Method Not Allowed", "Method not allowed");
    const WebAsset *asset = nullptr;
    for (const auto &candidate : web_assets)
        if (path == candidate.path) {
            asset = &candidate;
            break;
        }
    if (!asset) {
        if (path != "/" && path != "/history" && path != "/sensors" && path != "/alerts" &&
            path != "/settings" && path != "/setup")
            return error(r, "404 Not Found", "Not found");
        for (const auto &candidate : web_assets)
            if (std::strcmp(candidate.path, "/index.html") == 0) {
                asset = &candidate;
                break;
            }
    }
    if (!asset)
        return ESP_FAIL;
    if (header(r, "Accept-Encoding", 256).find("gzip") == std::string::npos)
        return error(r, "406 Not Acceptable", "Client must accept gzip");
    httpd_resp_set_type(r, asset->type);
    httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(r, "Vary", "Accept-Encoding");
    httpd_resp_set_hdr(r, "Cache-Control",
                       std::strcmp(asset->path, "/index.html") == 0
                           ? "no-cache"
                           : "public, max-age=31536000, immutable");
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(
        r, "Content-Security-Policy",
        "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src "
        "'self' ws:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");
    return httpd_resp_send(r, reinterpret_cast<const char *>(asset->data), asset->size);
}
} // namespace monitor
