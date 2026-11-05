#pragma once

namespace monitor {
// Runs one USB test-bridge command line (DNS_BRIDGE, HTTP_BRIDGE, WS_BRIDGE, WS_LOAD_BRIDGE or
// WS_SLOW_BRIDGE followed by a JSON request). Returns false when `line` is not a bridge command.
bool handle_usb_bridge_command(const char *line);
} // namespace monitor
