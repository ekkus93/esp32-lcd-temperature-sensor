#pragma once
// Device settings routes: GET (public display fields; everything for the owner) and PATCH.
#include "api_routes.hpp"
namespace monitor {
esp_err_t route_settings(RouteRequest &request, RoutePorts &ports);
} // namespace monitor
