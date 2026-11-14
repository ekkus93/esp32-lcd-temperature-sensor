// The route boundary harness (God file refactor G12; review audit A10.2 / gate H04). Every API
// route goes through the real dispatcher (dispatch_api: the pre-gate auth routes, the gate and the
// handler table) with recording fakes. For an anonymous caller, a session without the CSRF token,
// a session with the wrong token and a full session, a refused request must reach no service.
#include "api_dispatch.hpp"
#include "fake_exchange.hpp"
#include "fake_ports.hpp"
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
enum class Caller { anonymous, no_csrf, wrong_csrf, owner };
struct Outcome {
    std::string status, body;
    int side_effects;
    std::size_t queued;
    int authorize_calls;
};
// Sends one request with a plausible body through a fresh device.
Outcome send(const RouteSpec &route, Caller caller) {
    FakeDevice device;
    device.auth.session = Session{"token", "csrf-1", 3600000};
    FakeExchange ex;
    ex.request_method = route.method;
    ex.request_uri = sample(route);
    ex.async_available = true;
    ex.request_headers["Content-Type"] = "application/json";
    ex.body = R"({"erase_history":true})";
    if (caller != Caller::anonymous)
        ex.request_headers["Cookie"] = "tm_session=token";
    if (caller == Caller::owner)
        ex.request_headers["X-CSRF-Token"] = "csrf-1";
    if (caller == Caller::wrong_csrf)
        ex.request_headers["X-CSRF-Token"] = "csrf-2";
    const std::string path = sample(route);
    dispatch_api(ex, path, device.ports, 0);
    return {ex.status, ex.sent, device.side_effects(), device.host.queued.size(),
            device.auth.log.count("authorize")};
}
bool refused(const Outcome &outcome) {
    return outcome.status == "401 Unauthorized";
}
} // namespace

int main() {
    std::size_t count = 0;
    const RouteSpec *routes = api_routes(count);
    unsigned owner_routes = 0, changes = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto &route = routes[i];
        CHECK(handler_for(route.id) != nullptr);
        const auto access = route_access(http_method_name(route.method), sample(route));
        const bool csrf = route_needs_csrf(access, http_method_name(route.method));
        const auto anonymous = send(route, Caller::anonymous);
        const auto no_csrf = send(route, Caller::no_csrf);
        const auto wrong_csrf = send(route, Caller::wrong_csrf);
        const auto owner = send(route, Caller::owner);
        // A full session always passes the gate (a handler may still answer 404 for an unknown
        // probe, but never the gate's "Unknown API route").
        CHECK(!refused(owner) && owner.body.find("Unknown API route") == std::string::npos);
        if (access == RouteAccess::open) {
            CHECK(!refused(anonymous) && !refused(no_csrf));
            continue;
        }
        ++owner_routes;
        // Anonymous callers are refused and reach nothing.
        CHECK(refused(anonymous) && anonymous.side_effects == 0 && anonymous.queued == 0);
        if (csrf) {
            ++changes;
            for (const auto &outcome : {no_csrf, wrong_csrf})
                CHECK(refused(outcome) && outcome.side_effects == 0 && outcome.queued == 0);
        } else {
            CHECK(!refused(no_csrf)); // owner reads need the session, not the token
        }
    }
    CHECK(owner_routes == 15 && changes == 13);

    // The open auth routes run before the gate and do not consult the session store there.
    for (std::size_t i = 0; i < count; ++i)
        if (routes[i].before_gate)
            CHECK(send(routes[i], Caller::anonymous).authorize_calls ==
                  (routes[i].id == RouteId::auth_state ? 1 : 0));

    // Unknown routes and wrong methods are 404 for everyone and reach nothing.
    for (const auto &[method, path] : std::initializer_list<std::pair<HttpMethod, const char *>>{
             {HttpMethod::get, "/api/v1/nope"},
             {HttpMethod::post, "/api/v1/status"},
             {HttpMethod::put, "/api/v1/settings"},
             {HttpMethod::get, "/api/v1/system/factory-reset"},
             {HttpMethod::del, "/api/v1/sensors/284D086700000009"},
             {HttpMethod::other, "/api/v1/status"}}) {
        FakeDevice device;
        device.auth.session = Session{"token", "csrf-1", 3600000};
        FakeExchange ex;
        ex.request_method = method;
        ex.request_headers["Cookie"] = "tm_session=token";
        ex.request_headers["X-CSRF-Token"] = "csrf-1";
        dispatch_api(ex, path, device.ports, 0);
        CHECK(ex.status == "404 Not Found" && device.side_effects() == 0);
    }
    std::cout << "route boundary: " << count << " routes, " << owner_routes << " owner routes ("
              << changes << " changes) refused without a session and token, reaching nothing\n";
}
