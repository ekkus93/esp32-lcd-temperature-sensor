// Authentication routes (moved from HttpApi::handle and login_worker, God file refactor G04).
#include "routes_auth.hpp"
#include "api_responses.hpp"
#include "api_router.hpp"
#include <algorithm>
#include <cstdio>
namespace monitor {
esp_err_t route_auth_state(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    Json object(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddBoolToObject(object.get(), "enrolled", ports.auth.enrolled());
    const auto signed_in = request_session(ex, ports.auth, false, request.now_ms);
    cJSON_AddBoolToObject(object.get(), "signed_in", signed_in.has_value());
    if (signed_in)
        cJSON_AddStringToObject(object.get(), "csrf", signed_in->csrf.c_str());
    return send_json(ex, object.get());
}
esp_err_t route_auth_enroll(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    auto input = read_json_body(ex);
    if (!input)
        return send_error(ex, "400 Bad Request", "Expected bounded JSON object");
    const auto result = ports.auth.enroll(json_string(input.get(), "password"), request.now_ms,
                                          ports.network.info().provisioning);
    const auto reply = enrollment_reply(result);
    if (result != EnrollResult::enrolled) {
        char retry[12];
        if (reply.retry_after > 0) {
            std::snprintf(retry, sizeof(retry), "%d", reply.retry_after);
            ex.set_header("Retry-After", retry);
        }
        return send_error(ex, reply.status, reply.message);
    }
    ex.set_status(reply.status);
    return ex.send("{}", 2);
}
// Login: the slow password check runs on the login worker so other requests keep being served.
esp_err_t route_auth_login(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    auto input = read_json_body(ex);
    if (!input)
        return send_error(ex, "400 Bad Request", "Expected bounded JSON object");
    std::string password = json_string(input.get(), "password");
    const uint32_t client = ex.client();
    const auto decision = ports.auth.login_begin(client, request.now_ms);
    if (decision.gate != LoginGate::allow) {
        char retry[24];
        std::snprintf(
            retry, sizeof(retry), "%lld",
            static_cast<long long>(std::max<int64_t>(1, (decision.retry_after_ms + 999) / 1000)));
        ex.set_header("Retry-After", retry);
        return send_error(ex, "429 Too Many Requests",
                          decision.gate == LoginGate::busy
                              ? "Another login is being checked; retry shortly"
                              : "Too many failed logins; retry later");
    }
    auto async = ex.begin_async();
    if (!async || !ports.host.queue_login(std::move(async), std::move(password), client)) {
        ports.auth.login_abort(client);
        return send_error(ex, "503 Service Unavailable", "Login unavailable; retry shortly");
    }
    return ESP_OK; // the worker answers on the async copy
}
esp_err_t route_auth_logout(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    ports.auth.logout(request.session->token);
    ex.set_header("Set-Cookie", "tm_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    return ex.send("{}", 2);
}
esp_err_t route_auth_password(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    auto input = read_json_body(ex);
    if (!input || !ports.auth.change_password(
                      request.session->token, json_string(input.get(), "old_password"),
                      json_string(input.get(), "new_password"), request.now_ms))
        return send_error(ex, "403 Forbidden", "Password change denied");
    return ex.send("{}", 2);
}
void complete_login(HttpExchange &ex, AuthPort &auth, std::string &password, uint32_t client,
                    int64_t now_ms) {
    auto failure = LoginFailure::none;
    const auto session = auth.login_complete(password, client, now_ms, &failure);
    std::fill(password.begin(), password.end(), '\0');
    if (!session && failure == LoginFailure::wrong_password) {
        ex.set_header("Retry-After", "5");
        send_error(ex, "401 Unauthorized", "Login denied; retry after 5 seconds");
    } else if (!session) {
        // Not the password's fault: say so, so nobody retypes a correct password.
        ex.set_header("Retry-After", "5");
        send_error(ex, "503 Service Unavailable",
                   "The device could not check the password just now; try again in a few "
                   "seconds");
    } else {
        ex.set_header("Set-Cookie", "tm_session=" + session->token +
                                        "; Path=/; HttpOnly; SameSite=Strict; Max-Age=3600");
        Json object(cJSON_CreateObject(), cJSON_Delete);
        cJSON_AddStringToObject(object.get(), "csrf", session->csrf.c_str());
        send_json(ex, object.get());
    }
}
} // namespace monitor
