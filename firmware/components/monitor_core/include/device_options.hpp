#pragma once
#include "display_model.hpp"
namespace monitor {
struct DeviceOptions : DisplayOptions {
    std::string timezone = "America/Los_Angeles";
    std::string ntp_server = "pool.ntp.org";
    // The name the device answers to on the network: <hostname>.local (mDNS).
    std::string hostname = "temperature-monitor";
};
const char *timezone_rule(const std::string &identifier);
// One lowercase RFC 1123 label: 1-63 of a-z, 0-9 and '-', not starting or ending with '-'.
bool valid_hostname(const std::string &name);
bool valid_device_options(const DeviceOptions &options);
std::string encode_device_options(const DeviceOptions &options);
std::optional<DeviceOptions> decode_device_options(const std::string &bytes);
} // namespace monitor
