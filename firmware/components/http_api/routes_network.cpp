// Network routes (moved from HttpApi::handle, God file refactor G07).
#include "routes_network.hpp"
#include "api_responses.hpp"
#include <vector>
namespace monitor {
esp_err_t route_wifi_status(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    const auto info = ports.network.info();
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
esp_err_t route_wifi_scan_start(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    if (!ports.network.scan())
        return send_error(ex, "429 Too Many Requests", "Scan busy; wait at least 10 seconds");
    ex.set_status("202 Accepted");
    return ex.send("{}", 2);
}
esp_err_t route_wifi_scan_results(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    std::vector<ScannedNetwork> networks;
    bool scanning = false;
    if (!ports.network.scan_results(networks, scanning))
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
esp_err_t route_wifi_connect(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    auto input = read_json_body(ex);
    if (input && ports.network.candidate_busy())
        return send_error(ex, "409 Conflict", "A Wi-Fi connection attempt is already in progress");
    if (!input || !ports.network.connect(json_string(input.get(), "ssid"),
                                         json_string(input.get(), "password")))
        return send_error(ex, "400 Bad Request", "Invalid network settings or control busy");
    ex.set_status("202 Accepted");
    return ex.send("{}", 2);
}
esp_err_t route_wifi_provisioning(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    if (!ports.network.provision())
        return send_error(ex, "503 Service Unavailable", "Network control busy");
    ex.set_status("202 Accepted");
    return ex.send("{}", 2);
}
} // namespace monitor
