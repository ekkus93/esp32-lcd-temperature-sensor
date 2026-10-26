#pragma once
#include "device_options.hpp"
#include <cstdint>
#include <optional>
#include <string>
namespace monitor {
bool start_clock(const DeviceOptions &options = DeviceOptions{});
bool configure_clock(const DeviceOptions &options);
bool clock_synchronized();
std::optional<int64_t> trusted_utc();
std::string utc_text();
std::string local_time_text();
} // namespace monitor
