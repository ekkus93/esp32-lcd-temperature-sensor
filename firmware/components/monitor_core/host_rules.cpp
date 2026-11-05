#include "host_rules.hpp"
#include <cctype>

namespace monitor {
namespace {
bool same_text(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}
} // namespace

bool host_allowed(std::string_view host, const HostRules &rules) {
    constexpr std::string_view port = ":80";
    if (host.size() > port.size() && host.substr(host.size() - port.size()) == port)
        host.remove_suffix(port.size());
    if (host.empty())
        return false;
    // Names are matched without regard to case (DNS names are case-insensitive).
    auto named = [&](const std::string &name) {
        if (name.empty())
            return false;
        constexpr std::string_view suffix = ".local";
        return host.size() == name.size() + suffix.size() &&
               same_text(host.substr(0, name.size()), name) &&
               same_text(host.substr(name.size()), suffix);
    };
    if (!rules.setup_ap_ip.empty() && host == rules.setup_ap_ip)
        return true;
    if (!rules.station_ip.empty() && rules.station_ip != "0.0.0.0" && host == rules.station_ip)
        return true;
    return named(rules.configured_name) || named(rules.active_name);
}
} // namespace monitor
