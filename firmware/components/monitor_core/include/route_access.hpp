#pragma once
#include <string_view>

namespace monitor {
// Who may call an API route. Everything that only reads device state is public; anything that
// changes the device, or exposes the network setup, needs the owner's session.
enum class RouteAccess {
    unknown, // no such route: answered 404, with or without a session
    open,    // anyone who can reach the device (Host is still checked)
    owner,   // a signed-in owner; mutations also need the CSRF token
};
// `method` is "GET", "POST", "PATCH", "PUT" or "DELETE"; `path` excludes the query string.
RouteAccess route_access(std::string_view method, std::string_view path);

// The single check in front of every API handler (review audit H04). A request reaches a handler
// only when this says `proceed`; the HTTP dispatcher answers the others itself.
enum class ApiGate {
    not_found,    // 404: not in the table, for everyone
    unauthorized, // 401: an owner route without a valid session (and CSRF token for changes)
    proceed,
};
// Owner routes that change something need the CSRF token as well as the session cookie.
bool route_needs_csrf(RouteAccess access, std::string_view method);
// Whether a request's session counts: a valid session token and, when the route needs it, the
// matching CSRF token.
bool session_accepted(bool token_valid, bool csrf_required, bool csrf_matches);
ApiGate api_gate(RouteAccess access, bool session_ok);
} // namespace monitor
