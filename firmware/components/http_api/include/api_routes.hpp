#pragma once
// What every route handler receives (God file refactor, spec §4). Handlers are free functions in
// routes_*.cpp, over these interfaces only, so each can run on the host with fakes.
#include "api_ports.hpp"
#include "history_query.hpp"
#include "public_limiter.hpp"
#include <memory>
#include <optional>
#include <string>
namespace monitor {
// Work that belongs to HttpApi itself: its worker tasks, the public limiter and the hostname.
class RouteHost {
  public:
    virtual ~RouteHost() = default;
    // Hands an async answer to the login worker; false if it could not be queued (the exchange is
    // then completed, and the caller answers on the original request).
    virtual bool queue_login(std::unique_ptr<HttpExchange> exchange, std::string password,
                             uint32_t client) = 0;
    // Admission for public history requests (one per second, one export per 30 s, two at once).
    virtual PublicLimiter::Decision admit_public(PublicRequest kind, uint32_t client) = 0;
    virtual void finish_public(PublicRequest kind) = 0;
    // Hands an admitted history request to the history worker; false if it could not be queued
    // (the exchange is completed and the admission is not released: the caller does that).
    virtual bool queue_history(std::unique_ptr<HttpExchange> exchange, const HistoryQuery &query,
                               bool exporting, PublicRequest kind) = 0;
    // Follows a hostname change: the Host allow-list and the mDNS name.
    virtual void apply_hostname(const std::string &name) = 0;
};
struct RoutePorts {
    AuthPort &auth;
    NetworkPort &network;
    HistoryPort &history;
    DeviceSettingsPort &settings;
    SystemPort &system;
    SensorUpdates &updates;
    LiveState &live;
    RouteHost &host;
};
struct RouteRequest {
    HttpExchange &ex;
    const std::string &path;
    // The gate's session (empty for anonymous callers and for routes that run before the gate).
    const std::optional<Session> &session;
    int64_t now_ms;
};
} // namespace monitor
