#include "device_options.hpp"
#include <iomanip>
#include <locale>
#include <sstream>
namespace monitor {
const char *timezone_rule(const std::string &id) {
    // Explicit supported identifiers; POSIX tails from the host's IANA tzdata.
    static const std::pair<const char *, const char *> rules[] = {
        {"UTC", "UTC0"},
        {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
        {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
        {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
        {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Asia/Tokyo", "JST-9"},
        {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"}};
    for (const auto &rule : rules)
        if (id == rule.first)
            return rule.second;
    return nullptr;
}
bool valid_device_options(const DeviceOptions &o) {
    if (!valid_display_options(o) || !timezone_rule(o.timezone) || o.ntp_server.empty() ||
        o.ntp_server.size() > 253)
        return false;
    unsigned label = 0;
    bool hyphen = false;
    for (unsigned char c : o.ntp_server) {
        if (c == '.') {
            if (!label || hyphen)
                return false;
            label = 0;
        } else {
            const bool alnum =
                (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            if (!alnum && (c != '-' || !label))
                return false;
            if (++label > 63)
                return false;
        }
        hyphen = c == '-';
    }
    return label && !hyphen;
}
namespace {
uint32_t checksum(const std::string &text) {
    uint32_t value = 2166136261u;
    for (unsigned char c : text) {
        value ^= c;
        value *= 16777619u;
    }
    return value;
}
} // namespace
std::string encode_device_options(const DeviceOptions &o) {
    if (!valid_device_options(o))
        return {};
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "TMD1 " << o.fahrenheit << ' ' << o.extended << ' ' << o.overview_seconds << ' '
        << o.trends_seconds << ' ' << o.lookback_minutes << ' ' << std::quoted(o.timezone) << ' '
        << std::quoted(o.ntp_server) << '\n';
    auto data = out.str();
    return data + std::to_string(checksum(data));
}
std::optional<DeviceOptions> decode_device_options(const std::string &bytes) {
    if (bytes.size() > 512)
        return std::nullopt;
    const auto newline = bytes.rfind('\n');
    if (newline == std::string::npos)
        return std::nullopt;
    std::istringstream tail(bytes.substr(newline + 1));
    uint64_t expected = 0;
    if (!(tail >> expected) || expected != checksum(bytes.substr(0, newline + 1)))
        return std::nullopt;
    tail >> std::ws;
    if (!tail.eof())
        return std::nullopt;
    std::istringstream in(bytes.substr(0, newline + 1));
    in.imbue(std::locale::classic());
    DeviceOptions o;
    std::string magic;
    if (!(in >> magic >> o.fahrenheit >> o.extended >> o.overview_seconds >> o.trends_seconds >>
          o.lookback_minutes >> std::quoted(o.timezone) >> std::quoted(o.ntp_server)) ||
        magic != "TMD1")
        return std::nullopt;
    in >> std::ws;
    if (!in.eof() || !valid_device_options(o))
        return std::nullopt;
    return o;
}
} // namespace monitor
