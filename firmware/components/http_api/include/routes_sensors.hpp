#pragma once
// Sensor routes: the sensor list and storage status, active alerts with the event journal, sensor
// edits and retirement, and the ice-bath calibration hold.
#include "api_routes.hpp"
namespace monitor {
esp_err_t route_alerts(RouteRequest &request, RoutePorts &ports);
esp_err_t route_sensor_list(RouteRequest &request, RoutePorts &ports);
esp_err_t route_sensor_storage(RouteRequest &request, RoutePorts &ports);
// PUT (start or renew) and DELETE (end) /sensors/{rom}/calibration-hold.
esp_err_t route_calibration_hold(RouteRequest &request, RoutePorts &ports);
// PATCH (edit) and DELETE (retire) /sensors/{rom}.
esp_err_t route_sensor_change(RouteRequest &request, RoutePorts &ports);
} // namespace monitor
