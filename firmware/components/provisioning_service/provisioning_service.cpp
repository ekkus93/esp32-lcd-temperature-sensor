#include "provisioning_service.hpp"
#include "boot_button_service.hpp"
#include "cJSON.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "nvs.h"
#include "usb_bridge.hpp"
#include <cstdio>
#include <cstring>
namespace monitor {
namespace {
void console(void *context) {
    auto &wifi = *static_cast<WifiService *>(context);
    char line[4096]{};
    size_t used = 0;
    bool discard = false;
    while (true) {
        int character = std::fgetc(stdin);
        if (character == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (character == '\r')
            continue;
        if (character != '\n') {
            if (!discard && used + 2 < sizeof(line))
                line[used++] = static_cast<char>(character);
            else {
                discard = true;
                used = 0;
            }
            continue;
        }
        if (discard) {
            discard = false;
            used = 0;
            std::memset(line, 0, sizeof(line));
            continue;
        }
        line[used++] = '\n';
        line[used] = 0;
        if (std::strcmp(line, "RESET_ADMIN_CREDENTIALS CONFIRM\n") == 0) {
            nvs_handle_t handle;
            bool ok = nvs_open("monitor_auth", NVS_READWRITE, &handle) == ESP_OK;
            if (ok) {
                auto verifier = nvs_erase_key(handle, "verifier1");
                auto bootstrap = nvs_erase_key(handle, "bootstrap");
                ok = (verifier == ESP_OK || verifier == ESP_ERR_NVS_NOT_FOUND) &&
                     (bootstrap == ESP_OK || bootstrap == ESP_ERR_NVS_NOT_FOUND) &&
                     nvs_commit(handle) == ESP_OK;
                nvs_close(handle);
            }
            std::printf("\nADMIN_RESET_%s\n", ok ? "OK" : "FAILED");
            std::fflush(stdout);
            if (ok) {
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }
        } else if (std::strcmp(line, "SETUP_INFO\n") == 0) {
            // Explicit physical-USB response, never an ordinary log message.
            char bootstrap[65]{};
            size_t size = sizeof(bootstrap);
            nvs_handle_t handle;
            if (nvs_open("monitor_auth", NVS_READONLY, &handle) == ESP_OK) {
                if (nvs_get_str(handle, "bootstrap", bootstrap, &size) != ESP_OK)
                    bootstrap[0] = 0;
                nvs_close(handle);
            }
            cJSON *object = cJSON_CreateObject();
            cJSON_AddStringToObject(object, "ssid", wifi.ap_ssid().c_str());
            cJSON_AddStringToObject(object, "password", wifi.ap_password().c_str());
            if (bootstrap[0])
                cJSON_AddStringToObject(object, "setup_secret", bootstrap);
            else
                cJSON_AddNullToObject(object, "setup_secret");
            cJSON_AddStringToObject(object, "ip", "192.168.4.1");
            char *json = cJSON_PrintUnformatted(object);
            if (json) {
                std::printf("\nSETUP_INFO_BEGIN\n%s\nSETUP_INFO_END\n", json);
                cJSON_free(json);
            }
            cJSON_Delete(object);
            std::memset(bootstrap, 0, sizeof(bootstrap));
        } else if (handle_usb_bridge_command(line)) {
            // a test bridge ran (usb_bridge.cpp)
        } else if (std::strncmp(line, "WIFI_CONNECT ", 13) == 0) {
            cJSON *object = cJSON_Parse(line + 13);
            auto *ssid = cJSON_GetObjectItemCaseSensitive(object, "ssid");
            auto *password = cJSON_GetObjectItemCaseSensitive(object, "password");
            bool ok = cJSON_IsString(ssid) && cJSON_IsString(password) &&
                      wifi.connect(ssid->valuestring, password->valuestring);
            cJSON_Delete(object);
            std::printf("\nWIFI_CONNECT_RESULT %s\n", ok ? "accepted" : "rejected");
        }
        std::memset(line, 0, sizeof(line));
        used = 0;
    }
}
} // namespace
bool start_provisioning_controls(WifiService &wifi) {
    const bool button_started = start_boot_button_service(wifi);
    return xTaskCreate(console, "usb_setup", 16384, &wifi, 1, nullptr) == pdPASS && button_started;
}
} // namespace monitor
