// Device settings routes (moved from HttpApi::handle, God file refactor G06).
#include "routes_settings.hpp"
#include "api_responses.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>
namespace monitor {
namespace {
// Applies a PATCH body to `options`; false when any field is unknown, repeated or invalid.
bool apply_settings_patch(const cJSON *input, DeviceOptions &options) {
    std::vector<std::string> keys;
    const cJSON *field = nullptr;
    cJSON_ArrayForEach(field, input) {
        std::string key = field->string;
        if (std::find(keys.begin(), keys.end(), key) != keys.end())
            return false;
        keys.push_back(key);
        if (key == "units" || key == "display_mode" || key == "timezone" || key == "ntp_server" ||
            key == "hostname") {
            if (!cJSON_IsString(field))
                return false;
            std::string value = field->valuestring;
            if (key == "units") {
                if (value != "C" && value != "F")
                    return false;
                options.fahrenheit = value == "F";
            } else if (key == "display_mode") {
                if (value != "basic" && value != "extended")
                    return false;
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
                std::floor(field->valuedouble) != field->valuedouble)
                return false;
            const auto value = unsigned(field->valuedouble);
            if (key == "overview_seconds")
                options.overview_seconds = value;
            else if (key == "trends_seconds")
                options.trends_seconds = value;
            else
                options.lookback_minutes = value;
        } else
            return false;
    }
    // The schema requires at least one property (minProperties: 1).
    return !keys.empty() && valid_device_options(options);
}
// Saves a PATCH; on success `options`, `revision` and `writable` hold the stored settings.
// Returns ESP_OK to go on and answer with them, or the refusal already sent.
std::optional<esp_err_t> save_settings_patch(HttpExchange &ex, RoutePorts &ports,
                                             DeviceOptions &options, uint64_t &revision,
                                             bool &writable, const std::string &etag) {
    if (!writable)
        return send_error(ex, "503 Service Unavailable",
                          "Stored settings damaged; preserved for recovery");
    const auto match = ex.header("If-Match", 128);
    if (match.empty())
        return send_error(ex, "428 Precondition Required", "Refresh settings before saving");
    if (match != etag)
        return send_error(ex, "412 Precondition Failed", "Settings changed; refresh before saving");
    auto input = read_json_body(ex);
    if (!input)
        return send_error(ex, "400 Bad Request", "Expected bounded JSON object");
    if (!apply_settings_patch(input.get(), options))
        return send_error(ex, "400 Bad Request", "Invalid device settings");
    if (ports.settings.save(options, revision) != ESP_OK)
        return send_error(ex, "503 Service Unavailable", "Settings could not be committed");
    ports.host.apply_hostname(options.hostname);
    if (!ports.system.configure_clock(options))
        return send_error(ex, "503 Service Unavailable",
                          "Settings saved; clock configuration unavailable");
    if (!ports.settings.get(options, revision, writable))
        return send_error(ex, "503 Service Unavailable", "Settings saved; refresh to confirm");
    return std::nullopt;
}
} // namespace

esp_err_t route_settings(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    DeviceOptions options;
    uint64_t revision = 0;
    bool writable = false;
    if (!ports.settings.get(options, revision, writable))
        return send_error(ex, "503 Service Unavailable", "Settings busy");
    auto etag = [&]() {
        return "\"" + ports.live.boot_id() + ":settings:" + std::to_string(revision) + "\"";
    };
    if (ex.method() == HttpMethod::patch)
        if (const auto refused =
                save_settings_patch(ex, ports, options, revision, writable, etag()))
            return *refused;
    const bool owner = request.session.has_value();
    Json output(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddNumberToObject(output.get(), "schema_version", 1);
    // Owner-only fields (etag, revision, writable, ntp_server) are left out of the public
    // response (device-settings-public.schema.json); a signed-in owner gets them all.
    if (owner) {
        cJSON_AddStringToObject(output.get(), "etag", etag().c_str());
        cJSON_AddNumberToObject(output.get(), "revision", revision);
        cJSON_AddBoolToObject(output.get(), "writable", writable);
    }
    cJSON_AddStringToObject(output.get(), "units", options.fahrenheit ? "F" : "C");
    cJSON_AddStringToObject(output.get(), "display_mode", options.extended ? "extended" : "basic");
    cJSON_AddNumberToObject(output.get(), "overview_seconds", options.overview_seconds);
    cJSON_AddNumberToObject(output.get(), "trends_seconds", options.trends_seconds);
    cJSON_AddNumberToObject(output.get(), "lookback_minutes", options.lookback_minutes);
    cJSON_AddStringToObject(output.get(), "timezone", options.timezone.c_str());
    cJSON_AddStringToObject(output.get(), "hostname", options.hostname.c_str());
    // The NTP server is the owner's: anonymous readers get the display fields only.
    if (owner)
        cJSON_AddStringToObject(output.get(), "ntp_server", options.ntp_server.c_str());
    return send_json(ex, output.get());
}
} // namespace monitor
