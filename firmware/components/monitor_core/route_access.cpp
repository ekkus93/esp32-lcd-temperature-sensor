#include "route_access.hpp"

namespace monitor {
namespace {
constexpr std::string_view api = "/api/v1";
bool is_rom_route(std::string_view path) {
    // /sensors/{rom_id}: 16 lowercase hex digits, never "storage".
    constexpr std::string_view prefix = "/sensors/";
    if (path.substr(0, prefix.size()) != prefix)
        return false;
    const auto id = path.substr(prefix.size());
    if (id.size() != 16)
        return false;
    for (char c : id)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}
// /sensors/{rom_id}/calibration-hold: the ice-bath hold (spec 17.3).
bool is_hold_route(std::string_view path) {
    constexpr std::string_view suffix = "/calibration-hold";
    return path.size() > suffix.size() && path.substr(path.size() - suffix.size()) == suffix &&
           is_rom_route(path.substr(0, path.size() - suffix.size()));
}
} // namespace

RouteAccess route_access(std::string_view method, std::string_view full_path) {
    if (full_path.substr(0, api.size()) != api)
        return RouteAccess::unknown;
    const auto path = full_path.substr(api.size());
    const bool get = method == "GET", post = method == "POST", patch = method == "PATCH",
               put = method == "PUT", del = method == "DELETE";
    if (get) {
        for (const auto open :
             {"/status", "/history", "/history/export", "/alerts", "/sensors", "/sensors/storage",
              "/probe-scan", "/clock", "/settings", "/auth/state"})
            if (path == open)
                return RouteAccess::open;
        // The Wi-Fi network details and scans are the owner's.
        if (path == "/wifi/status" || path == "/wifi/scan")
            return RouteAccess::owner;
        return RouteAccess::unknown;
    }
    if (post) {
        if (path == "/auth/login" || path == "/auth/enroll")
            return RouteAccess::open;
        for (const auto owner :
             {"/auth/logout", "/auth/password", "/ota", "/system/reboot", "/system/factory-reset",
              "/wifi/scan", "/wifi/provisioning", "/wifi/connect"})
            if (path == owner)
                return RouteAccess::owner;
        return RouteAccess::unknown;
    }
    if (patch) {
        if (path == "/settings" || is_rom_route(path))
            return RouteAccess::owner;
        return RouteAccess::unknown;
    }
    if (put)
        return is_hold_route(path) ? RouteAccess::owner : RouteAccess::unknown;
    if (del)
        return is_rom_route(path) || is_hold_route(path) ? RouteAccess::owner
                                                         : RouteAccess::unknown;
    return RouteAccess::unknown;
}
bool route_needs_csrf(RouteAccess access, std::string_view method) {
    return access == RouteAccess::owner && method != "GET";
}
bool session_accepted(bool token_valid, bool csrf_required, bool csrf_matches) {
    return token_valid && (!csrf_required || csrf_matches);
}
ApiGate api_gate(RouteAccess access, bool session_ok) {
    if (access == RouteAccess::unknown)
        return ApiGate::not_found;
    if (access == RouteAccess::owner && !session_ok)
        return ApiGate::unauthorized;
    return ApiGate::proceed;
}
} // namespace monitor
