#pragma once
#include "wifi_service.hpp"
namespace monitor {
bool start_boot_button_service(WifiService &wifi);
unsigned boot_hold_progress();
} // namespace monitor
