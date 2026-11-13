#pragma once
// Authentication routes: state, enroll and login (before the gate), logout and password change.
#include "api_routes.hpp"
namespace monitor {
esp_err_t route_auth_state(RouteRequest &request, RoutePorts &ports);
esp_err_t route_auth_enroll(RouteRequest &request, RoutePorts &ports);
esp_err_t route_auth_login(RouteRequest &request, RoutePorts &ports);
esp_err_t route_auth_logout(RouteRequest &request, RoutePorts &ports);
esp_err_t route_auth_password(RouteRequest &request, RoutePorts &ports);
// The login worker's half: checks the password (slow) and answers the async exchange.
void complete_login(HttpExchange &exchange, AuthPort &auth, std::string &password, uint32_t client,
                    int64_t now_ms);
} // namespace monitor
