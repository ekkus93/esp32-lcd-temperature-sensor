#include "api_dispatch.hpp"
#include "api_responses.hpp"
#include "routes_auth.hpp"
#include "routes_history.hpp"
#include "routes_network.hpp"
#include "routes_sensors.hpp"
#include "routes_settings.hpp"
#include "routes_system.hpp"
namespace monitor {
RouteHandler handler_for(RouteId id) {
    switch (id) {
    case RouteId::auth_state:
        return route_auth_state;
    case RouteId::auth_enroll:
        return route_auth_enroll;
    case RouteId::auth_login:
        return route_auth_login;
    case RouteId::auth_logout:
        return route_auth_logout;
    case RouteId::auth_password:
        return route_auth_password;
    case RouteId::status:
        return route_status;
    case RouteId::clock:
        return route_clock;
    case RouteId::probe_scan:
        return route_probe_scan;
    case RouteId::history:
    case RouteId::history_export:
        return route_history;
    case RouteId::alerts:
        return route_alerts;
    case RouteId::sensors:
        return route_sensor_list;
    case RouteId::sensor_storage:
        return route_sensor_storage;
    case RouteId::sensor_update:
    case RouteId::sensor_retire:
        return route_sensor_change;
    case RouteId::calibration_hold_start:
    case RouteId::calibration_hold_end:
        return route_calibration_hold;
    case RouteId::settings_read:
    case RouteId::settings_update:
        return route_settings;
    case RouteId::wifi_status:
        return route_wifi_status;
    case RouteId::wifi_scan_results:
        return route_wifi_scan_results;
    case RouteId::wifi_scan_start:
        return route_wifi_scan_start;
    case RouteId::wifi_provisioning:
        return route_wifi_provisioning;
    case RouteId::wifi_connect:
        return route_wifi_connect;
    case RouteId::ota:
        return route_ota;
    case RouteId::reboot:
        return route_reboot;
    case RouteId::factory_reset:
        return route_factory_reset;
    }
    return nullptr;
}
esp_err_t dispatch_api(HttpExchange &ex, const std::string &path, RoutePorts &ports,
                       int64_t now_ms) {
    // The open auth routes run before the gate: they manage the session themselves.
    if (const auto early = match_route(ex.method(), path); early && early->before_gate) {
        const std::optional<Session> no_session;
        RouteRequest request{ex, path, no_session, now_ms};
        return handler_for(early->id)(request, ports);
    }
    // One place decides who may call what: the route table and route_access (api_router.hpp).
    // A request reaches a handler only when the gate says proceed; anything not in the table is a
    // 404 for everyone.
    const auto gate = gate_request(ex, ports.auth, path, now_ms);
    switch (gate.gate) {
    case ApiGate::not_found:
        return send_error(ex, "404 Not Found", "Unknown API route");
    case ApiGate::unauthorized:
        return send_error(ex, "401 Unauthorized", "Administrator session required");
    case ApiGate::proceed:
        break;
    }
    RouteRequest request{ex, path, gate.session, now_ms};
    return handler_for(gate.route->id)(request, ports);
}
} // namespace monitor
