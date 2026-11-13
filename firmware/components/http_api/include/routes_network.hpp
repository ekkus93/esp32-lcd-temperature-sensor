#pragma once
// Network routes (the owner's): Wi-Fi status, scans, connecting and the setup access point.
#include "api_routes.hpp"
namespace monitor {
esp_err_t route_wifi_status(RouteRequest &request, RoutePorts &ports);
esp_err_t route_wifi_scan_start(RouteRequest &request, RoutePorts &ports);
esp_err_t route_wifi_scan_results(RouteRequest &request, RoutePorts &ports);
esp_err_t route_wifi_connect(RouteRequest &request, RoutePorts &ports);
esp_err_t route_wifi_provisioning(RouteRequest &request, RoutePorts &ports);
} // namespace monitor
