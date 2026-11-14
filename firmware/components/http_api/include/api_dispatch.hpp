#pragma once
// One API request from the router to its handler (God file refactor G12): the open auth routes
// that run before the gate, then the gate, then the handler for the route's id.
#include "api_router.hpp"
#include "api_routes.hpp"
namespace monitor {
using RouteHandler = esp_err_t (*)(RouteRequest &, RoutePorts &);
// The handler that answers a route; every RouteId has one.
RouteHandler handler_for(RouteId id);
// Answers an /api/ request whose Host and Origin were already checked.
esp_err_t dispatch_api(HttpExchange &exchange, const std::string &path, RoutePorts &ports,
                       int64_t now_ms);
} // namespace monitor
