#pragma once
#include <string>
#include <string_view>

namespace monitor {
// Which `Host` header values the device answers to. A request for any other host (a name that
// points at the device from elsewhere, for example) is refused, which keeps DNS-rebinding pages
// from talking to the device.
struct HostRules {
    std::string setup_ap_ip = "192.168.4.1";
    std::string station_ip;      // empty until the station has an address
    std::string configured_name; // the hostname setting, without ".local"
    std::string active_name;     // the name mDNS is really using (it differs after a conflict)
};
// `host` is the header as received: a name or address with an optional ":80".
bool host_allowed(std::string_view host, const HostRules &rules);
} // namespace monitor
