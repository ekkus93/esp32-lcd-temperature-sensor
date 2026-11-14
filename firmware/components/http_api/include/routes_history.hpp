#pragma once
// History routes: chart queries and CSV exports. The HTTP task admits and queues a request; the
// history worker answers it (serve_history).
#include "api_routes.hpp"
namespace monitor {
// GET /history and /history/export: validates the query, applies the public limits and hands
// the request to the history worker.
esp_err_t route_history(RouteRequest &request, RoutePorts &ports);
// The worker's half: answers one chart query or CSV export on the async exchange.
esp_err_t serve_history(HttpExchange &exchange, RoutePorts &ports, const HistoryQuery &query,
                        bool exporting);
} // namespace monitor
