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
// `method` is "GET", "POST", "PATCH" or "DELETE"; `path` excludes the query string.
RouteAccess route_access(std::string_view method, std::string_view path);
} // namespace monitor
