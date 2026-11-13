#include "service_adapters.hpp"
#include "clock_service.hpp"
#include "esp_http_server.h"
#include "esp_system.h"
#include "ota_service.hpp"
namespace monitor {
OtaStatus SystemAdapter::ota_status() {
    return monitor::ota_status();
}
ClockDiagnostics SystemAdapter::clock_diagnostics() {
    return clock_sync_diagnostics();
}
bool SystemAdapter::reboot_available() {
    return monitor::reboot_available();
}
bool SystemAdapter::schedule_reboot() {
    return monitor::schedule_reboot();
}
void SystemAdapter::restart_now() {
    esp_restart();
}
bool SystemAdapter::schedule_factory_reset(bool erase_history) {
    return monitor::schedule_factory_reset(erase_history);
}
esp_err_t SystemAdapter::upload_firmware(HttpExchange &exchange) {
    // The upload streams with ESP-IDF directly; on the device the exchange is an IdfExchange.
    return handle_ota_upload(static_cast<httpd_req_t *>(exchange.native_handle()));
}
} // namespace monitor
