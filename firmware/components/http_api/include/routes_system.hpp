#pragma once
// System routes: the live status, clock diagnostics, the probe scan, firmware upload, restart and
// factory reset.
#include "api_routes.hpp"
namespace monitor {
esp_err_t route_status(RouteRequest &request, RoutePorts &ports);
esp_err_t route_clock(RouteRequest &request, RoutePorts &ports);
esp_err_t route_probe_scan(RouteRequest &request, RoutePorts &ports);
esp_err_t route_ota(RouteRequest &request, RoutePorts &ports);
esp_err_t route_reboot(RouteRequest &request, RoutePorts &ports);
esp_err_t route_factory_reset(RouteRequest &request, RoutePorts &ports);
} // namespace monitor
