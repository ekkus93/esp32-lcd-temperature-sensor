// Authentication routes over fakes (God file refactor G04): each answer, header and side effect.
#include "fake_exchange.hpp"
#include "fake_ports.hpp"
#include "routes_auth.hpp"
#include <cstdlib>
#include <iostream>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

namespace {
const std::optional<Session> none;
const std::optional<Session> owner = Session{"token", "csrf-1", 3600000};
FakeExchange post(const std::string &body) {
    FakeExchange ex;
    ex.request_method = HttpMethod::post;
    ex.body = body;
    ex.request_headers["Content-Type"] = "application/json";
    return ex;
}
esp_err_t run(esp_err_t (*route)(RouteRequest &, RoutePorts &), FakeExchange &ex,
              FakeDevice &device, const std::optional<Session> &session = none) {
    const std::string path = ex.request_uri;
    RouteRequest request{ex, path, session, 1000};
    return route(request, device.ports);
}
} // namespace

int main() {
    // GET /auth/state.
    {
        FakeDevice device;
        FakeExchange ex;
        CHECK(run(route_auth_state, ex, device) == ESP_OK);
        CHECK(ex.status == "200 OK" && ex.sent == R"({"enrolled":true,"signed_in":false})");
        device.auth.session = owner;
        device.auth.is_enrolled = false;
        FakeExchange signed_in;
        signed_in.request_headers["Cookie"] = "tm_session=token";
        run(route_auth_state, signed_in, device);
        CHECK(signed_in.sent == R"({"enrolled":false,"signed_in":true,"csrf":"csrf-1"})");
        CHECK(device.side_effects() == 0);
    }
    // POST /auth/enroll: every result.
    {
        FakeDevice device;
        auto empty = post("");
        CHECK(run(route_auth_enroll, empty, device) == ESP_OK);
        CHECK(empty.status == "400 Bad Request" && device.auth.log.count("enroll") == 0);
        device.network.network.provisioning = true;
        auto ok = post(R"({"password":"a long password"})");
        run(route_auth_enroll, ok, device);
        CHECK(ok.status == "201 Created" && ok.sent == "{}");
        CHECK(device.auth.last_password == "a long password" && device.auth.last_setup_open);
        const struct {
            EnrollResult result;
            const char *status;
            const char *retry;
        } refusals[] = {{EnrollResult::already_enrolled, "409 Conflict", ""},
                        {EnrollResult::window_closed, "403 Forbidden", ""},
                        {EnrollResult::too_soon, "429 Too Many Requests", "5"},
                        {EnrollResult::bad_password, "400 Bad Request", ""},
                        {EnrollResult::unavailable, "503 Service Unavailable", "5"}};
        for (const auto &refusal : refusals) {
            device.auth.enroll_result = refusal.result;
            auto ex = post(R"({"password":"x"})");
            run(route_auth_enroll, ex, device);
            CHECK(ex.status == refusal.status);
            CHECK(ex.response_header("Retry-After") == refusal.retry);
            CHECK(ex.sent.find("\"error\":") != std::string::npos);
        }
    }
    // POST /auth/login.
    {
        FakeDevice device;
        auto empty = post("");
        run(route_auth_login, empty, device);
        CHECK(empty.status == "400 Bad Request" && device.auth.log.count("login_begin") == 0);
        // Throttled: busy and locked, with the retry rounded up to whole seconds (at least 1).
        device.auth.login_decision = {LoginGate::busy, 1};
        auto busy = post(R"({"password":"p"})");
        run(route_auth_login, busy, device);
        CHECK(busy.status == "429 Too Many Requests" && busy.response_header("Retry-After") == "1");
        CHECK(busy.sent.find("Another login is being checked") != std::string::npos);
        device.auth.login_decision = {LoginGate::locked, 4001};
        auto locked = post(R"({"password":"p"})");
        run(route_auth_login, locked, device);
        CHECK(locked.response_header("Retry-After") == "5");
        CHECK(locked.sent.find("Too many failed logins") != std::string::npos);
        CHECK(device.host.log.count("queue_login") == 0);
        // Allowed, but no async answer is possible, or the worker queue is full: aborted, 503.
        device.auth.login_decision = {LoginGate::allow, 0};
        auto no_async = post(R"({"password":"p"})");
        run(route_auth_login, no_async, device);
        CHECK(no_async.status == "503 Service Unavailable");
        CHECK(device.auth.log.count("login_abort") == 1);
        device.host.accept = false;
        auto full = post(R"({"password":"p"})");
        full.async_available = true;
        run(route_auth_login, full, device);
        CHECK(full.status == "503 Service Unavailable" &&
              device.auth.log.count("login_abort") == 2);
        // Queued: nothing is answered on this request; the worker answers the async copy.
        device.host.accept = true;
        auto queued = post(R"({"password":"secret"})");
        queued.async_available = true;
        queued.peer = 42;
        CHECK(run(route_auth_login, queued, device) == ESP_OK);
        CHECK(queued.sends == 0 && device.host.queued.size() == 1);
        CHECK(device.host.last_password == "secret" && device.host.last_client == 42);
    }
    // The login worker's answer.
    {
        FakeAuth auth;
        FakeExchange wrong;
        std::string password = "secret";
        auth.login_failure = LoginFailure::wrong_password;
        complete_login(wrong, auth, password, 1, 0);
        CHECK(wrong.status == "401 Unauthorized" && wrong.response_header("Retry-After") == "5");
        CHECK(password == std::string(6, '\0')); // wiped
        FakeExchange unavailable;
        password = "secret";
        auth.login_failure = LoginFailure::unavailable;
        complete_login(unavailable, auth, password, 1, 0);
        CHECK(unavailable.status == "503 Service Unavailable");
        FakeExchange ok;
        password = "secret";
        auth.session = owner;
        complete_login(ok, auth, password, 1, 0);
        CHECK(ok.status == "200 OK" && ok.sent == R"({"csrf":"csrf-1"})");
        CHECK(ok.response_header("Set-Cookie") ==
              "tm_session=token; Path=/; HttpOnly; SameSite=Strict; Max-Age=3600");
    }
    // POST /auth/logout and /auth/password (behind the gate: a session is given).
    {
        FakeDevice device;
        FakeExchange logout;
        logout.request_method = HttpMethod::post;
        run(route_auth_logout, logout, device, owner);
        CHECK(device.auth.log.count("logout") == 1 && device.auth.last_token == "token");
        CHECK(logout.sent == "{}" &&
              logout.response_header("Set-Cookie") ==
                  "tm_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
        auto no_body = post("");
        run(route_auth_password, no_body, device, owner);
        CHECK(no_body.status == "403 Forbidden" && device.auth.log.count("change_password") == 0);
        device.auth.password_changed = false;
        auto denied = post(R"({"old_password":"a","new_password":"b"})");
        run(route_auth_password, denied, device, owner);
        CHECK(denied.status == "403 Forbidden");
        device.auth.password_changed = true;
        auto changed = post(R"({"old_password":"a","new_password":"b"})");
        run(route_auth_password, changed, device, owner);
        CHECK(changed.status == "200 OK" && changed.sent == "{}");
    }
    std::cout << "auth routes: state, enroll, login (and its worker), logout, password\n";
}
