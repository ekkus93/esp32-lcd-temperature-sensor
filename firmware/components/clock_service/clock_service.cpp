#include "clock_service.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <atomic>
#include <cstdlib>
#include <ctime>
namespace monitor {
namespace {
std::atomic<bool> synchronized{false};
std::atomic<uint32_t> sync_generation{0};
std::atomic<int64_t> last_sync_utc_seconds{-1};
std::atomic<int64_t> last_sync_monotonic_ms{-1};
SemaphoreHandle_t clock_mutex = nullptr;
bool initialized = false;
bool ip_handler_registered = false;
std::string server;

// SNTP is started with the radio, before the station has an address. lwIP then backs off its
// retries exponentially (15 s doubling to 150 s) after the first request fails, so the clock
// could stay unsynchronized for minutes although the network was up within seconds. Restarting
// SNTP when an address arrives resets that backoff and sends the first real query immediately.
void restart_sntp_on_new_address(void *, esp_event_base_t, int32_t, void *) {
    if (esp_sntp_restart())
        ESP_LOGI("clock", "Network address acquired; SNTP restarted");
}
} // namespace
bool start_clock(const DeviceOptions &options) {
    clock_mutex = xSemaphoreCreateMutex();
    if (!clock_mutex || !configure_clock(options))
        return false;
    if (!ip_handler_registered) {
        ip_handler_registered =
            esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, restart_sntp_on_new_address,
                                       nullptr) == ESP_OK;
        if (!ip_handler_registered)
            ESP_LOGW("clock", "Could not watch for the network address; SNTP retries may be slow");
    }
    return true;
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
        config.sync_cb = [](struct timeval *time) {
            if (!time)
                return;
            sync_generation.fetch_add(1, std::memory_order_acq_rel);
            last_sync_utc_seconds.store(time->tv_sec, std::memory_order_relaxed);
            last_sync_monotonic_ms.store(esp_timer_get_time() / 1000, std::memory_order_relaxed);
            synchronized.store(true, std::memory_order_release);
            sync_generation.fetch_add(1, std::memory_order_release);
        };
        initialized = esp_netif_sntp_init(&config) == ESP_OK;
        ok = initialized;
    }
    xSemaphoreGive(clock_mutex);
    return ok;
}
bool clock_synchronized() {
    return synchronized.load();
}
ClockDiagnostics clock_sync_diagnostics() {
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const auto before = sync_generation.load(std::memory_order_acquire);
        if (before & 1)
            continue;
        const auto utc = last_sync_utc_seconds.load(std::memory_order_relaxed);
        const auto monotonic = last_sync_monotonic_ms.load(std::memory_order_relaxed);
        const auto after = sync_generation.load(std::memory_order_acquire);
        if (before == after)
            return make_clock_diagnostics(utc < 0 ? std::nullopt : std::optional<int64_t>(utc),
                                          monotonic < 0 ? std::nullopt
                                                        : std::optional<int64_t>(monotonic),
                                          esp_timer_get_time() / 1000);
    }
    return {};
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
