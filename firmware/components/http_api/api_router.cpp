#include "api_router.hpp"
#include "api_responses.hpp"
#include "sessions.hpp"
namespace monitor {
namespace {
constexpr HttpMethod GET = HttpMethod::get, POST = HttpMethod::post, PUT = HttpMethod::put,
                     PATCH = HttpMethod::patch, DEL = HttpMethod::del;
constexpr RoutePattern EXACT = RoutePattern::exact;
constexpr RouteSpec routes[] = {
    {GET, EXACT, "/api/v1/auth/state", RouteId::auth_state, true},
    {POST, EXACT, "/api/v1/auth/enroll", RouteId::auth_enroll, true},
    {POST, EXACT, "/api/v1/auth/login", RouteId::auth_login, true},
    {POST, EXACT, "/api/v1/auth/logout", RouteId::auth_logout, false},
    {POST, EXACT, "/api/v1/auth/password", RouteId::auth_password, false},
    {GET, EXACT, "/api/v1/status", RouteId::status, false},
    {GET, EXACT, "/api/v1/clock", RouteId::clock, false},
    {GET, EXACT, "/api/v1/probe-scan", RouteId::probe_scan, false},
    {GET, EXACT, "/api/v1/history", RouteId::history, false},
    {GET, EXACT, "/api/v1/history/export", RouteId::history_export, false},
    {GET, EXACT, "/api/v1/alerts", RouteId::alerts, false},
    {GET, EXACT, "/api/v1/sensors", RouteId::sensors, false},
    {GET, EXACT, "/api/v1/sensors/storage", RouteId::sensor_storage, false},
    {PATCH, RoutePattern::sensor, "/api/v1/sensors/{rom}", RouteId::sensor_update, false},
    {DEL, RoutePattern::sensor, "/api/v1/sensors/{rom}", RouteId::sensor_retire, false},
    {PUT, RoutePattern::calibration_hold, "/api/v1/sensors/{rom}/calibration-hold",
     RouteId::calibration_hold_start, false},
    {DEL, RoutePattern::calibration_hold, "/api/v1/sensors/{rom}/calibration-hold",
     RouteId::calibration_hold_end, false},
    {GET, EXACT, "/api/v1/settings", RouteId::settings_read, false},
    {PATCH, EXACT, "/api/v1/settings", RouteId::settings_update, false},
    {GET, EXACT, "/api/v1/wifi/status", RouteId::wifi_status, false},
    {GET, EXACT, "/api/v1/wifi/scan", RouteId::wifi_scan_results, false},
    {POST, EXACT, "/api/v1/wifi/scan", RouteId::wifi_scan_start, false},
    {POST, EXACT, "/api/v1/wifi/provisioning", RouteId::wifi_provisioning, false},
    {POST, EXACT, "/api/v1/wifi/connect", RouteId::wifi_connect, false},
    {POST, EXACT, "/api/v1/ota", RouteId::ota, false},
    {POST, EXACT, "/api/v1/system/reboot", RouteId::reboot, false},
    {POST, EXACT, "/api/v1/system/factory-reset", RouteId::factory_reset, false},
};
constexpr std::string_view sensor_prefix = "/api/v1/sensors/";
constexpr std::string_view hold_suffix = "/calibration-hold";
bool is_rom(std::string_view id) {
    if (id.size() != 16)
        return false;
    for (char c : id)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}
bool matches(const RouteSpec &route, std::string_view path) {
    switch (route.pattern) {
    case RoutePattern::exact:
        return path == route.path;
    case RoutePattern::sensor:
        return path.substr(0, sensor_prefix.size()) == sensor_prefix &&
               is_rom(path.substr(sensor_prefix.size()));
    case RoutePattern::calibration_hold:
        return path.size() > sensor_prefix.size() + hold_suffix.size() &&
               path.substr(0, sensor_prefix.size()) == sensor_prefix &&
               path.substr(path.size() - hold_suffix.size()) == hold_suffix &&
               is_rom(path.substr(sensor_prefix.size(),
                                  path.size() - sensor_prefix.size() - hold_suffix.size()));
    }
    return false;
}
} // namespace

const RouteSpec *api_routes(std::size_t &count) {
    count = sizeof(routes) / sizeof(routes[0]);
    return routes;
}
std::optional<RouteSpec> match_route(HttpMethod method, std::string_view path) {
    for (const auto &route : routes)
        if (route.method == method && matches(route, path))
            return route;
    return std::nullopt;
}
std::optional<Session> request_session(HttpExchange &exchange, AuthPort &auth, bool csrf_required,
                                       int64_t now_ms) {
    auto value = auth.authorize(session_cookie(exchange), now_ms);
    if (!session_accepted(value.has_value(), csrf_required,
                          value &&
                              constant_equal(exchange.header("X-CSRF-Token", 128), value->csrf)))
        return std::nullopt;
    return value;
}
GateResult gate_request(HttpExchange &exchange, AuthPort &auth, std::string_view path,
                        int64_t now_ms) {
    GateResult result;
    const auto method = exchange.method();
    const auto access = route_access(http_method_name(method), path);
    const auto route = match_route(method, path);
    // Owner routes need a session (and, for changes, the CSRF token). Open routes work without
    // one; a signed-in owner is still recognised so owner-only fields can be added.
    result.session =
        request_session(exchange, auth, route_needs_csrf(access, http_method_name(method)), now_ms);
    // A route the policy does not know, or the table does not answer, is a 404 for everyone.
    result.gate = route ? api_gate(access, result.session.has_value()) : ApiGate::not_found;
    if (result.gate == ApiGate::proceed)
        result.route = route;
    return result;
}
} // namespace monitor
