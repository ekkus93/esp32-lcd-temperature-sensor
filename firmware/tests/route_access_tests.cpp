#include "route_access.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
int main() {
    const std::string rom = "284d086700000009";
    // Read-only routes are open to anyone.
    for (const char *path :
         {"/status", "/history", "/history/export", "/alerts", "/sensors", "/sensors/storage",
          "/probe-scan", "/clock", "/settings", "/auth/state"})
        CHECK(route_access("GET", std::string("/api/v1") + path) == RouteAccess::open);
    CHECK(route_access("POST", "/api/v1/auth/login") == RouteAccess::open);
    CHECK(route_access("POST", "/api/v1/auth/enroll") == RouteAccess::open);
    // Changing anything, and the network setup, is the owner's.
    CHECK(route_access("PATCH", "/api/v1/settings") == RouteAccess::owner);
    CHECK(route_access("PATCH", "/api/v1/sensors/" + rom) == RouteAccess::owner);
    CHECK(route_access("DELETE", "/api/v1/sensors/" + rom) == RouteAccess::owner);
    // The ice-bath hold (spec 17.3) is the owner's: starting, renewing and ending it.
    CHECK(route_access("PUT", "/api/v1/sensors/" + rom + "/calibration-hold") ==
          RouteAccess::owner);
    CHECK(route_access("DELETE", "/api/v1/sensors/" + rom + "/calibration-hold") ==
          RouteAccess::owner);
    CHECK(route_access("GET", "/api/v1/sensors/" + rom + "/calibration-hold") ==
          RouteAccess::unknown);
    CHECK(route_access("PUT", "/api/v1/sensors/" + rom) == RouteAccess::unknown);
    CHECK(route_access("PUT", "/api/v1/sensors/storage/calibration-hold") == RouteAccess::unknown);
    CHECK(route_access("PUT", "/api/v1/sensors/284D086700000009/calibration-hold") ==
          RouteAccess::unknown);
    CHECK(route_access("PATCH", "/api/v1/sensors/" + rom + "/calibration-hold") ==
          RouteAccess::unknown);
    for (const char *path :
         {"/auth/logout", "/auth/password", "/ota", "/system/reboot", "/system/factory-reset",
          "/wifi/scan", "/wifi/provisioning", "/wifi/connect"})
        CHECK(route_access("POST", std::string("/api/v1") + path) == RouteAccess::owner);
    CHECK(route_access("GET", "/api/v1/wifi/status") == RouteAccess::owner);
    CHECK(route_access("GET", "/api/v1/wifi/scan") == RouteAccess::owner);
    // The same path with another method is not silently open: a read route does not make its
    // mutating counterpart reachable.
    CHECK(route_access("POST", "/api/v1/settings") == RouteAccess::unknown);
    CHECK(route_access("DELETE", "/api/v1/settings") == RouteAccess::unknown);
    CHECK(route_access("POST", "/api/v1/status") == RouteAccess::unknown);
    CHECK(route_access("PATCH", "/api/v1/history") == RouteAccess::unknown);
    CHECK(route_access("GET", "/api/v1/ota") == RouteAccess::unknown);
    CHECK(route_access("GET", "/api/v1/system/reboot") == RouteAccess::unknown);
    CHECK(route_access("GET", "/api/v1/auth/logout") == RouteAccess::unknown);
    CHECK(route_access("PUT", "/api/v1/settings") == RouteAccess::unknown);
    // Unknown routes stay unknown, whatever the method.
    CHECK(route_access("GET", "/api/v1/nope") == RouteAccess::unknown);
    CHECK(route_access("GET", "/api/v1") == RouteAccess::unknown);
    CHECK(route_access("GET", "/api/v2/status") == RouteAccess::unknown);
    CHECK(route_access("GET", "/status") == RouteAccess::unknown);
    CHECK(route_access("GET", "/api/v1/status/") == RouteAccess::unknown);
    CHECK(route_access("GET", "/api/v1/../status") == RouteAccess::unknown);
    // Sensor ids must be exactly 16 lowercase hex digits; "storage" and look-alikes are not ids.
    CHECK(route_access("PATCH", "/api/v1/sensors/storage") == RouteAccess::unknown);
    CHECK(route_access("PATCH", "/api/v1/sensors/284D086700000009") == RouteAccess::unknown);
    CHECK(route_access("PATCH", "/api/v1/sensors/284d08670000000") == RouteAccess::unknown);
    CHECK(route_access("PATCH", "/api/v1/sensors/" + rom + "0") == RouteAccess::unknown);
    CHECK(route_access("DELETE", "/api/v1/sensors") == RouteAccess::unknown);
    CHECK(route_access("GET", "/api/v1/sensors/" + rom) == RouteAccess::unknown);

    // Review audit H04: every route the HTTP code handles (kept in step with http_api.cpp by
    // tools/tests/test_route_inventory.py), through the gate the dispatcher runs before any
    // handler, for an anonymous caller, a session without the CSRF token, and a full session.
    struct Route {
        const char *method;
        std::string path;
        RouteAccess expected;
        bool destructive; // changes stored state, restarts or erases
    };
    const std::string hold = "/api/v1/sensors/" + rom + "/calibration-hold";
    const Route inventory[] = {
        {"GET", "/api/v1/status", RouteAccess::open, false},
        {"GET", "/api/v1/history", RouteAccess::open, false},
        {"GET", "/api/v1/history/export", RouteAccess::open, false},
        {"GET", "/api/v1/alerts", RouteAccess::open, false},
        {"GET", "/api/v1/sensors", RouteAccess::open, false},
        {"GET", "/api/v1/sensors/storage", RouteAccess::open, false},
        {"GET", "/api/v1/probe-scan", RouteAccess::open, false},
        {"GET", "/api/v1/clock", RouteAccess::open, false},
        {"GET", "/api/v1/settings", RouteAccess::open, false},
        {"GET", "/api/v1/auth/state", RouteAccess::open, false},
        {"POST", "/api/v1/auth/login", RouteAccess::open, false},
        {"POST", "/api/v1/auth/enroll", RouteAccess::open, true},
        {"GET", "/api/v1/wifi/status", RouteAccess::owner, false},
        {"GET", "/api/v1/wifi/scan", RouteAccess::owner, false},
        {"POST", "/api/v1/wifi/scan", RouteAccess::owner, false},
        {"POST", "/api/v1/wifi/provisioning", RouteAccess::owner, true},
        {"POST", "/api/v1/wifi/connect", RouteAccess::owner, true},
        {"POST", "/api/v1/auth/logout", RouteAccess::owner, true},
        {"POST", "/api/v1/auth/password", RouteAccess::owner, true},
        {"POST", "/api/v1/ota", RouteAccess::owner, true},
        {"POST", "/api/v1/system/reboot", RouteAccess::owner, true},
        {"POST", "/api/v1/system/factory-reset", RouteAccess::owner, true},
        {"PATCH", "/api/v1/settings", RouteAccess::owner, true},
        {"PATCH", "/api/v1/sensors/" + rom, RouteAccess::owner, true},
        {"DELETE", "/api/v1/sensors/" + rom, RouteAccess::owner, true},
        {"PUT", hold, RouteAccess::owner, true},
        {"DELETE", hold, RouteAccess::owner, true},
    };
    for (const auto &route : inventory) {
        const auto access = route_access(route.method, route.path);
        CHECK(access == route.expected);
        const bool csrf = route_needs_csrf(access, route.method);
        // Every owner change needs the CSRF token; owner reads and open routes do not.
        CHECK(csrf == (route.expected == RouteAccess::owner && std::string(route.method) != "GET"));
        const auto anonymous = api_gate(access, session_accepted(false, csrf, false));
        const auto no_csrf = api_gate(access, session_accepted(true, csrf, false));
        const auto signed_in = api_gate(access, session_accepted(true, csrf, true));
        if (route.expected == RouteAccess::open) {
            CHECK(anonymous == ApiGate::proceed && no_csrf == ApiGate::proceed &&
                  signed_in == ApiGate::proceed);
        } else {
            CHECK(anonymous == ApiGate::unauthorized);
            CHECK(no_csrf == (csrf ? ApiGate::unauthorized : ApiGate::proceed));
            CHECK(signed_in == ApiGate::proceed);
        }
        // No destructive owner route is reachable without a session and its CSRF token.
        if (route.destructive && route.expected == RouteAccess::owner)
            CHECK(anonymous != ApiGate::proceed && no_csrf != ApiGate::proceed);
    }
    CHECK(api_gate(route_access("POST", "/api/v1/nope"), true) == ApiGate::not_found);
    std::cout << "PASS: API route access policy and the dispatcher's gate for every route\n";
}
