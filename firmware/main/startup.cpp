// Start-up steps (moved from app_main, God file refactor G16).
#include "startup.hpp"
#include "board_pins.h"
#include "captive_service.hpp"
#include "clock_service.hpp"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "ota_service.hpp"
#include "provisioning_service.hpp"
namespace monitor {
namespace {
const char *tag = "monitor";
}
void prepare_storage() {
    esp_err_t nvs = nvs_flash_init();
    // Preserve existing NVS on corruption/schema mismatch; report and keep running.
    if (nvs != ESP_OK)
        ESP_LOGE(tag, "NVS unavailable: %s; no automatic erase", esp_err_to_name(nvs));
    else {
        const auto reset = apply_pending_factory_reset();
        if (reset != ESP_OK)
            ESP_LOGE(tag, "Factory reset could not be completed: %s; reset intent preserved",
                     esp_err_to_name(reset));
    }
}
void load_sensor_settings(SettingsService &settings, Registry &registry) {
    const auto load = settings.load(registry);
    if (!settings.writable())
        ESP_LOGE(tag, "Sensor settings unavailable: %s; original data preserved",
                 esp_err_to_name(load));
}
DeviceOptions load_device_settings(DeviceSettingsService &device_settings, bool simulated) {
    const auto device_load = device_settings.initialize(simulated);
    if (device_load != ESP_OK)
        ESP_LOGE(tag, "Device settings unavailable: %s; original data preserved",
                 esp_err_to_name(device_load));
    DeviceOptions options;
    uint64_t revision = 0;
    bool writable = false;
    device_settings.get(options, revision, writable);
    return options;
}
void log_build() {
    ESP_LOGI(tag, "version=%s idf=%s psram=%u internal_heap=%u", esp_app_get_description()->version,
             esp_get_idf_version(), static_cast<unsigned>(esp_psram_get_size()),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    ESP_LOGI(tag, "reset_reason=%d", static_cast<int>(esp_reset_reason()));
}
uint64_t new_boot_id() {
    return (uint64_t(esp_random()) << 32) | esp_random();
}
bool start_history(HistoryService &history, bool simulated, uint64_t boot_id) {
    const bool started = history.start(simulated, boot_id);
    if (!started)
        ESP_LOGE(tag, "History worker unavailable");
    return started;
}
void start_network(WifiService &wifi, AuthService &auth, HttpApi &api, HistoryService &history,
                   DeviceSettingsService &device_settings, const DeviceOptions &options,
                   uint64_t boot_id, bool simulated, bool sensor_settings_writable,
                   StartupHealth &startup) {
    const auto network_status = wifi.start(options.hostname);
    startup.wifi_stack_started = network_status == ESP_OK;
    if (network_status != ESP_OK) {
        ESP_LOGE(tag, "Wi-Fi init failed: %s", esp_err_to_name(network_status));
        return;
    }
    if (!start_clock(options))
        ESP_LOGW(tag, "Clock synchronization unavailable");
    const auto auth_status = auth.initialize();
    startup.authentication_started = auth_status == ESP_OK;
    if (startup.authentication_started) {
        startup.http_server_started = api.start(auth, wifi, history, device_settings, boot_id,
                                                simulated, sensor_settings_writable);
        if (!startup.http_server_started)
            ESP_LOGE(tag, "HTTP server unavailable");
        if (!start_captive_dns(wifi))
            ESP_LOGE(tag, "Captive DNS unavailable; direct AP address remains available");
    } else
        ESP_LOGE(tag,
                 "Authentication unavailable: %s; HTTP remains closed. Use the USB recovery "
                 "console (RESET_ADMIN_CREDENTIALS CONFIRM) to reset it.",
                 esp_err_to_name(auth_status));
    // The USB recovery console and the BOOT button service do not depend on authentication: they
    // are the way out when the stored credentials are damaged.
    if (!start_provisioning_controls(wifi))
        ESP_LOGE(tag, "Provisioning controls unavailable");
}
bool start_display(DisplayRuntime &display, DisplayTaskContext &context) {
    if (!display.initialize())
        ESP_LOGE(tag, "LCD unavailable; sensor/network services continue without display");
    const bool task_started =
        display.ready() && context.mutex &&
        xTaskCreate(display_refresh_task, "display_refresh", 6144, &context, 3, nullptr) == pdPASS;
    if (display.ready() && !task_started)
        ESP_LOGE(tag, "Independent display refresh unavailable; using acquisition cadence");
    return task_started;
}
bool start_sampling(SamplingService &sampling, bool simulated) {
    const esp_err_t bus_status = sampling.initialize(simulated);
    if (bus_status != ESP_OK)
        ESP_LOGE(tag, "Bus init failed: %s", esp_err_to_name(bus_status));
    ESP_LOGI(tag, "READY no peripherals required; DQ=%d; backlight disabled", PIN_ONEWIRE_DATA);
    return bus_status == ESP_OK;
}
void journal_boot(HistoryService &history, uint64_t boot_id, bool simulated) {
    JournalEvent boot_event{};
    boot_event.boot_id = boot_id;
    boot_event.kind = EventKind::reboot;
    if (!simulated)
        boot_event.utc_seconds = trusted_utc();
    history.submit_event(boot_event);
}
} // namespace monitor
