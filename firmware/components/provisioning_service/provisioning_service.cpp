#include "provisioning_service.hpp"
#include "board_pins.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "monitor.hpp"
#include "nvs.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <strings.h>
#include <unistd.h>
#include <vector>
namespace monitor {
namespace {
std::atomic<unsigned> hold_progress{0};
void button(void *context) {
    auto &wifi = *static_cast<WifiService *>(context);
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << PIN_BOOT;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&config);
    BootButton button;
    while (true) {
        const int64_t now = esp_timer_get_time() / 1000;
        const bool provision =
            button.update(gpio_get_level(static_cast<gpio_num_t>(PIN_BOOT)) == 0, now);
        hold_progress.store(button.progress(now));
        if (provision)
            wifi.provision();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void bridge(cJSON *request) {
    auto *path = cJSON_GetObjectItemCaseSensitive(request, "path");
    auto *method = cJSON_GetObjectItemCaseSensitive(request, "method");
    auto *payload = cJSON_GetObjectItemCaseSensitive(request, "body");
    if (!cJSON_IsString(path) || path->valuestring[0] != '/' || !cJSON_IsString(method))
        return;
    std::string uri = path->valuestring;
    if (uri.size() > CONFIG_HTTPD_MAX_URI_LEN ||
        uri.find_first_of("\r\n") != std::string::npos)
        return;
    esp_http_client_method_t verb = HTTP_METHOD_GET;
    if (std::strcmp(method->valuestring, "POST") == 0)
        verb = HTTP_METHOD_POST;
    else if (std::strcmp(method->valuestring, "PATCH") == 0)
        verb = HTTP_METHOD_PATCH;
    else if (std::strcmp(method->valuestring, "GET") != 0)
        return;
    std::string url = "http://127.0.0.1" + uri;
    struct Metadata {
        std::string cookie, encoding, location;
    } metadata;
    esp_http_client_config_t config{};
    config.url = url.c_str();
    config.timeout_ms = 60000;
    config.disable_auto_redirect = true;
    config.user_data = &metadata;
    config.event_handler = [](esp_http_client_event_t *event) -> esp_err_t {
        if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
            std::strlen(event->header_value) < 512) {
            auto &metadata = *static_cast<Metadata *>(event->user_data);
            if (strcasecmp(event->header_key, "Set-Cookie") == 0)
                metadata.cookie = event->header_value;
            if (strcasecmp(event->header_key, "Content-Encoding") == 0)
                metadata.encoding = event->header_value;
            if (strcasecmp(event->header_key, "Location") == 0)
                metadata.location = event->header_value;
        }
        return ESP_OK;
    };
    auto client = esp_http_client_init(&config);
    if (!client)
        return;
    esp_http_client_set_method(client, verb);
    esp_http_client_set_header(client, "Host", "192.168.4.1");
    auto *headers = cJSON_GetObjectItemCaseSensitive(request, "headers");
    bool content_type_supplied = false;
    if (cJSON_IsObject(headers)) {
        cJSON *field = nullptr;
        unsigned count = 0;
        cJSON_ArrayForEach(field, headers) {
            if (++count > 8 || !cJSON_IsString(field) || std::strlen(field->valuestring) > 512)
                break;
            if (strcasecmp(field->string, "Content-Type") == 0)
                content_type_supplied = true;
            esp_http_client_set_header(client, field->string, field->valuestring);
        }
    }
    std::string data = cJSON_IsString(payload) ? payload->valuestring : "";
    if (!data.empty() && !content_type_supplied)
        esp_http_client_set_header(client, "Content-Type", "application/json");
    auto result = esp_http_client_open(client, data.size());
    if (result == ESP_OK && !data.empty() &&
        esp_http_client_write(client, data.data(), data.size()) != int(data.size()))
        result = ESP_FAIL;
    if (result == ESP_OK && esp_http_client_fetch_headers(client) < 0)
        result = ESP_FAIL;
    int status = result == ESP_OK ? esp_http_client_get_status_code(client) : 0;
    std::vector<char> response(8193, 0);
    const bool headers_only =
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(request, "headers_only"));
    int length = result == ESP_OK && status != 101 && !headers_only
                     ? esp_http_client_read_response(client, response.data(), 8192)
                     : 0;
    cJSON *output = cJSON_CreateObject();
    cJSON_AddNumberToObject(output, "status", status);
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(request, "binary"))) {
        auto *bytes = cJSON_AddArrayToObject(output, "bytes");
        for (int i = 0; i < length; ++i)
            cJSON_AddItemToArray(bytes,
                                 cJSON_CreateNumber(static_cast<unsigned char>(response[i])));
    } else
        cJSON_AddStringToObject(output, "body", length > 0 ? response.data() : "");
    if (!metadata.cookie.empty())
        cJSON_AddStringToObject(output, "cookie", metadata.cookie.c_str());
    if (!metadata.encoding.empty())
        cJSON_AddStringToObject(output, "content_encoding", metadata.encoding.c_str());
    if (!metadata.location.empty())
        cJSON_AddStringToObject(output, "location", metadata.location.c_str());
    char *json = cJSON_PrintUnformatted(output);
    if (json) {
        std::printf("\nHTTP_BRIDGE_BEGIN\n%s\nHTTP_BRIDGE_END\n", json);
        cJSON_free(json);
    }
    cJSON_Delete(output);
    esp_http_client_cleanup(client);
    std::fill(data.begin(), data.end(), 0);
}
void dns_bridge(cJSON *request) {
    auto *bytes = cJSON_GetObjectItemCaseSensitive(request, "bytes");
    if (!cJSON_IsArray(bytes) || cJSON_GetArraySize(bytes) > 512)
        return;
    std::vector<uint8_t> input;
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, bytes) {
        if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || item->valuedouble < 0 ||
            item->valuedouble > 255 || std::floor(item->valuedouble) != item->valuedouble)
            return;
        input.push_back(uint8_t(item->valuedouble));
    }
    std::array<uint8_t, 513> response{};
    int count = -1;
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd >= 0) {
        struct timeval timeout {
            2, 0
        };
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        sockaddr_in destination{};
        destination.sin_family = AF_INET;
        destination.sin_port = htons(53);
        destination.sin_addr.s_addr = inet_addr("192.168.4.1");
        if (sendto(socket_fd, input.data(), input.size(), 0,
                   reinterpret_cast<sockaddr *>(&destination),
                   sizeof(destination)) == int(input.size()))
            count = recv(socket_fd, response.data(), response.size(), 0);
        close(socket_fd);
    }
    auto *output = cJSON_CreateObject();
    cJSON_AddBoolToObject(output, "ok", count > 0 && count <= 512);
    auto *values = cJSON_AddArrayToObject(output, "bytes");
    if (count > 0 && count <= 512)
        for (int i = 0; i < count; ++i)
            cJSON_AddItemToArray(values, cJSON_CreateNumber(response[i]));
    char *text = cJSON_PrintUnformatted(output);
    if (text) {
        std::printf("\nDNS_BRIDGE_BEGIN\n%s\nDNS_BRIDGE_END\n", text);
        cJSON_free(text);
    }
    cJSON_Delete(output);
}
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
        } else if (std::strncmp(line, "DNS_BRIDGE ", 11) == 0) {
            cJSON *request = cJSON_Parse(line + 11);
            if (request) {
                dns_bridge(request);
                cJSON_Delete(request);
            }
        } else if (std::strncmp(line, "HTTP_BRIDGE ", 12) == 0) {
            cJSON *request = cJSON_Parse(line + 12);
            if (request) {
                bridge(request);
                cJSON_Delete(request);
            }
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
    bool ok = xTaskCreate(button, "boot_button", 3072, &wifi, 2, nullptr) == pdPASS;
    return xTaskCreate(console, "usb_setup", 16384, &wifi, 1, nullptr) == pdPASS && ok;
}
unsigned boot_hold_progress() {
    return hold_progress.load();
}
} // namespace monitor
