// Sensor routes (moved from HttpApi::handle, God file refactor G05).
#include "routes_sensors.hpp"
#include "api_responses.hpp"
#include "calibration_hold_request.hpp"
#include "live_json.hpp"
#include "retirement_policy.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <vector>
namespace monitor {
namespace {
const Sensor *find_sensor(const Registry &registry, std::string_view id) {
    const auto found =
        std::find_if(registry.sensors().begin(), registry.sensors().end(),
                     [&](const Sensor &sensor) { return rom_hex(sensor.rom) == id; });
    return found == registry.sensors().end() ? nullptr : &*found;
}
esp_err_t send_queued(HttpExchange &ex) {
    ex.set_status("202 Accepted");
    return send_text(ex, "{\"status\":\"queued\"}");
}
// `after` and `limit` from the alert query string; false when malformed.
bool parse_alert_cursor(const std::string &query_text, uint64_t &after, unsigned &limit) {
    std::vector<std::string> keys;
    for (size_t start = 0; start < query_text.size();) {
        auto end = query_text.find('&', start);
        if (end == std::string::npos)
            end = query_text.size();
        const auto item = query_text.substr(start, end - start);
        const auto equals = item.find('=');
        const auto key = item.substr(0, equals);
        const auto value = equals == std::string::npos ? std::string{} : item.substr(equals + 1);
        uint64_t number = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
        if (value.empty() || std::find(keys.begin(), keys.end(), key) != keys.end() ||
            parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
            return false;
        keys.push_back(key);
        if (key == "after")
            after = number;
        else if (key == "limit" && number >= 1 && number <= 50)
            limit = static_cast<unsigned>(number);
        else
            return false;
        start = end + 1;
    }
    return true;
}
// The conditions that need attention now: bus fault, lost network, probes out of range or gone.
void add_active_conditions(cJSON *active, const Registry &current, const NetworkInfo &network,
                           int64_t uptime_now, std::optional<int64_t> utc_now) {
    // When each condition began: uptime ms, and UTC seconds once the clock is trusted (computed
    // now, so a condition that began before the clock synced still gets one).
    auto add_since = [&](cJSON *event, int64_t since_ms) {
        const int64_t since = std::clamp<int64_t>(since_ms, 0, uptime_now);
        cJSON_AddNumberToObject(event, "since_uptime_ms", static_cast<double>(since));
        if (utc_now)
            cJSON_AddNumberToObject(event, "since_utc_seconds",
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
    // Losing the network is a condition that needs attention until it recovers. The connect and
    // disconnect transitions themselves are journal events.
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
}
void add_journal_page(cJSON *output, const JournalPage &page) {
    auto *events = cJSON_AddArrayToObject(output, "events");
    cJSON_AddBoolToObject(output, "event_log_healthy", page.available);
    if (page.oldest_available)
        cJSON_AddNumberToObject(output, "oldest_available",
                                static_cast<double>(page.oldest_available));
    else
        cJSON_AddNullToObject(output, "oldest_available");
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
    cJSON_AddNumberToObject(output, "next_after", static_cast<double>(page.next_after));
    cJSON_AddBoolToObject(output, "has_more", page.has_more);
}
// Applies a PATCH body to `settings`; false when any field is unknown, repeated or invalid.
bool apply_sensor_patch(const cJSON *input, Settings &settings) {
    const cJSON *field = nullptr;
    std::vector<std::string> keys;
    cJSON_ArrayForEach(field, input) {
        std::string key = field->string;
        if (std::find(keys.begin(), keys.end(), key) != keys.end())
            return false;
        keys.push_back(key);
        if (key == "name" || key == "short_name") {
            if (!cJSON_IsString(field))
                return false;
            (key == "name" ? settings.name : settings.short_name) = field->valuestring;
        } else if (key == "enabled" || key == "disconnect_alert") {
            if (!cJSON_IsBool(field))
                return false;
            (key == "enabled" ? settings.enabled : settings.disconnect_alert) = cJSON_IsTrue(field);
        } else if (key == "low_c" || key == "high_c") {
            std::optional<double> value;
            if (cJSON_IsNumber(field))
                value = field->valuedouble;
            else if (!cJSON_IsNull(field))
                return false;
            (key == "low_c" ? settings.low : settings.high) = value;
        } else if (key == "order" || key == "calibration_c" || key == "hysteresis_c" ||
                   key == "persistence_seconds") {
            if (!cJSON_IsNumber(field) || !std::isfinite(field->valuedouble))
                return false;
            const double value = field->valuedouble;
            if (key == "order") {
                if (value < 0 || value > 4 || std::floor(value) != value)
                    return false;
                settings.order = unsigned(value);
            } else if (key == "calibration_c")
                settings.calibration = value;
            else if (key == "hysteresis_c")
                settings.hysteresis = value;
            else {
                if (value < 0 || value > 3600)
                    return false;
                settings.persistence_ms = int64_t(std::round(value * 1000));
            }
        } else
            return false;
    }
    return !keys.empty() && valid_settings(settings);
}
esp_err_t queue_answer(HttpExchange &ex, QueueResult result, const char *memory, const char *full) {
    switch (result) {
    case QueueResult::no_memory:
        return send_error(ex, "503 Service Unavailable", memory);
    case QueueResult::full:
        return send_error(ex, "503 Service Unavailable", full);
    case QueueResult::queued:
        break;
    }
    return send_queued(ex);
}
} // namespace

esp_err_t route_alerts(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    LiveView view;
    if (!ports.live.read(view))
        return send_error(ex, "503 Service Unavailable", "Snapshot busy");
    uint64_t after = 0;
    unsigned limit = 50;
    const auto query_text = url_query(ex);
    if (query_text.size() > 64)
        return send_error(ex, "400 Bad Request", "Invalid alert cursor");
    if (!query_text.empty() && !parse_alert_cursor(query_text, after, limit))
        return send_error(ex, "400 Bad Request", "Invalid alert cursor");
    Json output(cJSON_CreateObject(), cJSON_Delete);
    auto *active = cJSON_AddArrayToObject(output.get(), "active");
    add_active_conditions(active, view.registry, ports.network.info(), request.now_ms,
                          ports.system.trusted_utc());
    add_journal_page(output.get(), ports.history.event_page(after, limit));
    return send_json(ex, output.get());
}
esp_err_t route_sensor_list(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    LiveView view;
    if (!ports.live.read(view))
        return send_error(ex, "503 Service Unavailable", "Snapshot busy");
    Json output(cJSON_CreateArray(), cJSON_Delete);
    for (const auto &sensor : view.registry.sensors())
        cJSON_AddItemToArray(output.get(), sensor_json(sensor, ports.live.boot_id()));
    return send_json(ex, output.get());
}
esp_err_t route_sensor_storage(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    LiveView view; // read as before, so a busy snapshot answers 503 here too
    if (!ports.live.read(view))
        return send_error(ex, "503 Service Unavailable", "Snapshot busy");
    Json output(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddNumberToObject(output.get(), "schema_version", 1);
    const bool writable = ports.updates.sensor_settings_writable();
    cJSON_AddStringToObject(output.get(), "state", writable ? "ready" : "read_only");
    cJSON_AddBoolToObject(output.get(), "writable", writable);
    return send_json(ex, output.get());
}
// Ice-bath calibration hold (spec 17.3). It is not a setting: no ETag, and it is kept in RAM, so
// it does not touch sensor storage.
esp_err_t route_calibration_hold(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    LiveView view;
    if (!ports.live.read(view))
        return send_error(ex, "503 Service Unavailable", "Snapshot busy");
    constexpr std::string_view hold_suffix = "/calibration-hold";
    const auto &path = request.path;
    const auto *found = find_sensor(
        view.registry, std::string_view(path).substr(16, path.size() - 16 - hold_suffix.size()));
    if (!found)
        return send_error(ex, "404 Not Found", "Unknown ROM ID");
    int64_t duration_ms = 0;
    if (ex.method() == HttpMethod::put) {
        constexpr int64_t minute_ms = 60000;
        if (!probe_can_be_held(found->state))
            return send_error(ex, "409 Conflict", "Probe must be connected to calibrate it");
        auto input = read_json_body(ex);
        if (!input)
            return send_error(ex, "400 Bad Request", "Expected bounded JSON object");
        const auto minutes = calibration_hold_minutes(input.get());
        if (!minutes)
            return send_error(ex, "400 Bad Request", "Hold for 1 to 20 minutes: {\"minutes\": n}");
        duration_ms = int64_t(*minutes) * minute_ms;
    } else if (ex.method() == HttpMethod::del) {
        if (ex.content_length() != 0)
            return send_error(ex, "400 Bad Request", "Ending a hold does not accept a body");
    } else {
        return send_error(ex, "405 Method Not Allowed", "Method not allowed");
    }
    return queue_answer(ex,
                        ports.updates.queue_calibration_hold(found->rom, duration_ms,
                                                             ex.method() == HttpMethod::del),
                        "Calibration memory unavailable", "Calibration request queue full");
}
esp_err_t route_sensor_change(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    LiveView view;
    if (!ports.live.read(view))
        return send_error(ex, "503 Service Unavailable", "Snapshot busy");
    if (!ports.updates.sensor_settings_writable())
        return send_error(ex, "503 Service Unavailable",
                          "Sensor settings are read-only; stored data is preserved for recovery");
    const auto *found = find_sensor(view.registry, std::string_view(request.path).substr(16));
    if (!found)
        return send_error(ex, "404 Not Found", "Unknown ROM ID");
    if (ports.updates.update_pending(view.generation))
        return send_error(ex, "409 Conflict",
                          "Another sensor update is pending; wait for the next sample");
    const std::string expected = "\"" + ports.live.boot_id() + ":" + rom_hex(found->rom) + ":" +
                                 std::to_string(found->config_revision) + "\"";
    const auto match = ex.header("If-Match", 128);
    if (match.empty())
        return send_error(ex, "428 Precondition Required",
                          "Refresh sensor settings before updating");
    if (match != expected)
        return send_error(ex, "412 Precondition Failed",
                          "Sensor settings changed; refresh before saving");
    if (ex.method() == HttpMethod::del) {
        if (ex.content_length() != 0)
            return send_error(ex, "400 Bad Request", "Retirement does not accept a body");
        if (!can_retire_sensor(found->state, found->rom, view.probe_scan, ports.live.simulated()))
            return send_error(ex, "409 Conflict",
                              "Probe must be disconnected and absent from a complete scan");
        return queue_answer(ex,
                            ports.updates.queue_sensor_update(found->rom, found->settings, true),
                            "Configuration memory unavailable", "Configuration queue full");
    }
    auto input = read_json_body(ex);
    if (!input)
        return send_error(ex, "400 Bad Request", "Expected bounded JSON object");
    Settings settings = found->settings;
    if (!apply_sensor_patch(input.get(), settings))
        return send_error(ex, "400 Bad Request", "Invalid sensor settings");
    return queue_answer(ex, ports.updates.queue_sensor_update(found->rom, settings, false),
                        "Configuration memory unavailable", "Configuration queue full");
}
} // namespace monitor
