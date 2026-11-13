// The API route table and the gate (God file refactor G03). The table and route_access() must name
// the same routes both ways, and a request reaches a handler only when the gate allows it.
#include "api_router.hpp"
#include "fake_exchange.hpp"
#include "fake_ports.hpp"
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

namespace {
const std::string rom = "284d086700000009";
std::string sample(const RouteSpec &route) {
    switch (route.pattern) {
    case RoutePattern::exact:
        return route.path;
    case RoutePattern::sensor:
        return "/api/v1/sensors/" + rom;
    case RoutePattern::calibration_hold:
        return "/api/v1/sensors/" + rom + "/calibration-hold";
    }
    return {};
}
const HttpMethod methods[] = {HttpMethod::get, HttpMethod::post, HttpMethod::put, HttpMethod::patch,
                              HttpMethod::del};
FakeExchange request(HttpMethod method, const std::string &path, const char *cookie,
                     const char *csrf) {
    FakeExchange exchange;
    exchange.request_method = method;
    exchange.request_uri = path;
    if (cookie)
        exchange.request_headers["Cookie"] = std::string("tm_session=") + cookie;
    if (csrf)
        exchange.request_headers["X-CSRF-Token"] = csrf;
    return exchange;
}
} // namespace

int main() {
    std::size_t count = 0;
    const RouteSpec *routes = api_routes(count);
    CHECK(count == 27);

    // Candidate paths: every table path, the policy's paths and look-alikes.
    std::set<std::string> paths;
    for (std::size_t i = 0; i < count; ++i)
        paths.insert(sample(routes[i]));
    for (const char *extra :
         {"/api/v1", "/api/v1/", "/api/v1/nope", "/api/v1/status/", "/api/v1/sensors/storage/x",
          "/api/v1/sensors/284D086700000009", "/api/v1/sensors/284d08670000000",
          "/api/v1/sensors/storage/calibration-hold", "/api/v2/status", "/status"})
        paths.insert(extra);
    // Both ways: a (method, path) the policy knows is in the table, and the reverse.
    std::set<RouteId> seen;
    for (const auto &path : paths)
        for (const auto method : methods) {
            const bool policy =
                route_access(http_method_name(method), path) != RouteAccess::unknown;
            const auto route = match_route(method, path);
            CHECK(policy == route.has_value());
            if (route)
                seen.insert(route->id);
        }
    CHECK(seen.size() == count); // every entry is reachable, and no two share an id

    // Only the open auth routes run before the gate.
    std::set<RouteId> early;
    for (std::size_t i = 0; i < count; ++i)
        if (routes[i].before_gate) {
            early.insert(routes[i].id);
            CHECK(route_access(http_method_name(routes[i].method), routes[i].path) ==
                  RouteAccess::open);
        }
    CHECK(early ==
          (std::set<RouteId>{RouteId::auth_state, RouteId::auth_enroll, RouteId::auth_login}));

    // The gate for each route: anonymous, a session without CSRF, a full session.
    FakeAuth auth;
    auth.session = Session{"token", "csrf-1", 3600000};
    for (std::size_t i = 0; i < count; ++i) {
        const auto &route = routes[i];
        const auto path = sample(route);
        const auto access = route_access(http_method_name(route.method), path);
        const bool csrf = route_needs_csrf(access, http_method_name(route.method));
        auto anonymous = request(route.method, path, nullptr, nullptr);
        auto no_csrf = request(route.method, path, "token", nullptr);
        auto full = request(route.method, path, "token", "csrf-1");
        auto wrong_csrf = request(route.method, path, "token", "csrf-2");
        const auto a = gate_request(anonymous, auth, path, 0);
        const auto n = gate_request(no_csrf, auth, path, 0);
        const auto f = gate_request(full, auth, path, 0);
        const auto w = gate_request(wrong_csrf, auth, path, 0);
        CHECK(f.gate == ApiGate::proceed && f.route && f.route->id == route.id && f.session);
        if (access == RouteAccess::open) {
            CHECK(a.gate == ApiGate::proceed && !a.session);
            CHECK(n.gate == ApiGate::proceed);
        } else {
            CHECK(a.gate == ApiGate::unauthorized && !a.route);
            CHECK(n.gate == (csrf ? ApiGate::unauthorized : ApiGate::proceed));
            CHECK(w.gate == (csrf ? ApiGate::unauthorized : ApiGate::proceed));
        }
        // No answer is sent by the gate itself.
        CHECK(anonymous.sends == 0 && full.sends == 0);
    }
    // Unknown routes are 404 whoever asks.
    for (const char *path : {"/api/v1/nope", "/api/v1/status/", "/api/v1/sensors/storage/x"}) {
        auto signed_in = request(HttpMethod::get, path, "token", "csrf-1");
        CHECK(gate_request(signed_in, auth, path, 0).gate == ApiGate::not_found);
    }
    auto wrong_method = request(HttpMethod::put, "/api/v1/status", "token", "csrf-1");
    CHECK(gate_request(wrong_method, auth, "/api/v1/status", 0).gate == ApiGate::not_found);
    // An expired or unknown cookie is no session.
    auto stale = request(HttpMethod::get, "/api/v1/wifi/status", "old-token", nullptr);
    CHECK(gate_request(stale, auth, "/api/v1/wifi/status", 0).gate == ApiGate::unauthorized);
    std::cout << "API route table matches the access policy; the gate guards every route\n";
}
