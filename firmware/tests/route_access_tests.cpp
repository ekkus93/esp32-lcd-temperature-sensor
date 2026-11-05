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
    std::cout << "PASS: API route access policy\n";
}
