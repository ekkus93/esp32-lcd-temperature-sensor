#pragma once
#include "wifi_service.hpp"
namespace monitor {
bool start_provisioning_controls(WifiService &wifi);
unsigned boot_hold_progress();
} // namespace monitor
