#pragma once
#include "esp_http_server.h"
#include <string>

namespace monitor {
struct OtaStatus {
    std::string version;
    std::string partition;
    std::string state;
};
OtaStatus ota_status();
bool reboot_available();
bool schedule_reboot();
esp_err_t handle_ota_upload(httpd_req_t *request);
} // namespace monitor
