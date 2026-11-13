#pragma once
// The API route table and the gate in front of every handler (God file refactor, spec §4.4).
// route_access() stays the single access policy; this table says which handler answers each
// (method, path), and a host test keeps the two equal.
#include "api_ports.hpp"
#include "http_exchange.hpp"
#include "route_access.hpp"
#include <optional>
#include <string_view>
namespace monitor {
enum class RouteId : uint8_t {
    auth_state,
    auth_enroll,
    auth_login,
    auth_logout,
    auth_password,
    status,
    clock,
    probe_scan,
    history,
    history_export,
    alerts,
    sensors,
    sensor_storage,
    sensor_update,
    sensor_retire,
    calibration_hold_start,
    calibration_hold_end,
    settings_read,
    settings_update,
    wifi_status,
    wifi_scan_results,
    wifi_scan_start,
    wifi_provisioning,
    wifi_connect,
    ota,
    reboot,
    factory_reset,
};
enum class RoutePattern : uint8_t {
    exact,            // `path` itself
    sensor,           // /api/v1/sensors/{rom}
    calibration_hold, // /api/v1/sensors/{rom}/calibration-hold
};
struct RouteSpec {
    HttpMethod method;
    RoutePattern pattern;
    const char *path; // the exact path, or the prefix pattern's example
    RouteId id;
    // Answered before the gate: the open auth routes, which also manage the session themselves.
    bool before_gate;
};
// Every API route the device answers.
const RouteSpec *api_routes(std::size_t &count);
std::optional<RouteSpec> match_route(HttpMethod method, std::string_view path);

// A request's session: a valid session cookie and, when `csrf_required`, the matching CSRF token.
std::optional<Session> request_session(HttpExchange &exchange, AuthPort &auth, bool csrf_required,
                                       int64_t now_ms);
struct GateResult {
    ApiGate gate = ApiGate::not_found;
    std::optional<RouteSpec> route; // set when the gate says proceed
    std::optional<Session> session; // a recognised session, also on open routes
};
// The gate (route_access, CSRF and session): a request reaches a handler only on `proceed`.
GateResult gate_request(HttpExchange &exchange, AuthPort &auth, std::string_view path,
                        int64_t now_ms);
} // namespace monitor
