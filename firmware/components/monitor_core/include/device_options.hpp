#pragma once
#include "display_model.hpp"
namespace monitor {
struct DeviceOptions : DisplayOptions {
    std::string timezone = "UTC";
    std::string ntp_server = "pool.ntp.org";
};
const char *timezone_rule(const std::string &identifier);
bool valid_device_options(const DeviceOptions &options);
std::string encode_device_options(const DeviceOptions &options);
std::optional<DeviceOptions> decode_device_options(const std::string &bytes);
} // namespace monitor
