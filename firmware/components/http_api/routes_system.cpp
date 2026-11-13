// System routes (moved from HttpApi::handle, God file refactor G08).
#include "routes_system.hpp"
#include "api_responses.hpp"
#include "live_json.hpp"
namespace monitor {
esp_err_t route_status(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    const auto text = snapshot_json(ports);
    if (text.empty())
        return send_error(ex, "503 Service Unavailable", "Snapshot busy");
    ex.set_type("application/json");
    ex.set_header("Cache-Control", "no-store");
    return ex.send(text.data(), text.size());
}
esp_err_t route_clock(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    const auto status = ports.system.clock_diagnostics();
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
esp_err_t route_probe_scan(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    LiveView view;
    if (!ports.live.read(view))
        return send_error(ex, "503 Service Unavailable", "Probe scan status busy");
    const ProbeScanStatus &status = view.probe_scan;
    Json object(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddNumberToObject(object.get(), "schema_version", 1);
    cJSON_AddBoolToObject(object.get(), "enabled", status.enabled);
    cJSON_AddBoolToObject(object.get(), "complete", status.complete);
    cJSON_AddNumberToObject(object.get(), "discovered_count", status.discovered_count);
    cJSON_AddBoolToObject(object.get(), "overflow", status.overflow);
    return send_json(ex, object.get());
}
esp_err_t route_ota(RouteRequest &request, RoutePorts &ports) {
    return ports.system.upload_firmware(request.ex);
}
esp_err_t route_reboot(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    if (ex.content_length() != 0) {
        const auto result =
            send_error(ex, "400 Bad Request", "Reboot request must not include a body");
        ex.close_connection();
        return result;
    }
    if (!ports.system.reboot_available())
        return send_error(ex, "503 Service Unavailable", "Could not schedule restart");
    ex.set_status("202 Accepted");
    ex.set_type("application/json");
    ex.set_header("Cache-Control", "no-store");
    const auto result = send_text(ex, "{\"status\":\"rebooting\"}");
    if (!ports.system.schedule_reboot())
        ports.system.restart_now();
    return result;
}
esp_err_t route_factory_reset(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    auto input = read_json_body(ex);
    if (!input || cJSON_GetArraySize(input.get()) != 1)
        return send_error(ex, "400 Bad Request", "Expected erase_history boolean");
    const auto *erase = cJSON_GetObjectItemCaseSensitive(input.get(), "erase_history");
    if (!cJSON_IsBool(erase))
        return send_error(ex, "400 Bad Request", "Expected erase_history boolean");
    if (!ports.system.schedule_factory_reset(cJSON_IsTrue(erase)))
        return send_error(ex, "503 Service Unavailable", "Could not schedule factory reset");
    ex.set_status("202 Accepted");
    ex.set_type("application/json");
    ex.set_header("Cache-Control", "no-store");
    return send_text(ex, "{\"status\":\"resetting\"}");
}
} // namespace monitor
