#include "clock_service.hpp"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <atomic>
#include <cstdlib>
#include <ctime>
namespace monitor {
namespace {
std::atomic<bool> synchronized{false};
SemaphoreHandle_t clock_mutex = nullptr;
bool initialized = false;
std::string server;
} // namespace
bool start_clock(const DeviceOptions &options) {
    clock_mutex = xSemaphoreCreateMutex();
    return clock_mutex && configure_clock(options);
}
bool configure_clock(const DeviceOptions &options) {
    if (!valid_device_options(options) || !clock_mutex ||
        xSemaphoreTake(clock_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;
    bool ok = setenv("TZ", timezone_rule(options.timezone), 1) == 0;
    if (ok)
        tzset();
    if (ok && (!initialized || server != options.ntp_server)) {
        if (initialized)
            esp_netif_sntp_deinit();
        initialized = false;
        server = options.ntp_server;
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server.c_str());
        config.sync_cb = [](struct timeval *) { synchronized = true; };
        initialized = esp_netif_sntp_init(&config) == ESP_OK;
        ok = initialized;
    }
    xSemaphoreGive(clock_mutex);
    return ok;
}
bool clock_synchronized() {
    return synchronized.load();
}
std::optional<int64_t> trusted_utc() {
    if (!synchronized)
        return std::nullopt;
    return std::time(nullptr);
}
std::string utc_text() {
    auto utc = trusted_utc();
    if (!utc)
        return {};
    std::time_t value = *utc;
    struct tm broken {};
    gmtime_r(&value, &broken);
    char text[32]{};
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &broken);
    return text;
}
std::string local_time_text() {
    auto utc = trusted_utc();
    if (!utc || !clock_mutex || xSemaphoreTake(clock_mutex, pdMS_TO_TICKS(10)) != pdTRUE)
        return {};
    std::time_t value = *utc;
    struct tm broken {};
    localtime_r(&value, &broken);
    char text[8]{};
    std::strftime(text, sizeof(text), "%H:%M", &broken);
    xSemaphoreGive(clock_mutex);
    return text;
}
} // namespace monitor
