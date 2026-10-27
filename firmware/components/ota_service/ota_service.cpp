#include "ota_service.hpp"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <string>

namespace monitor {
namespace {
std::atomic<bool> upload_active{false};
std::atomic<bool> reset_active{false};
esp_timer_handle_t reboot_timer = nullptr;
constexpr char reset_namespace[] = "monitor_system";
constexpr char reset_key[] = "factory_reset";

esp_err_t response(httpd_req_t *request, const char *status, const char *message) {
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    std::string body = "{\"error\":\"" + std::string(message) + "\"}";
    return httpd_resp_send(request, body.c_str(), body.size());
}

void reboot(void *) {
    esp_restart();
}

bool create_reboot_timer() {
    if (reboot_timer)
        return true;
    esp_timer_create_args_t args{};
    args.callback = reboot;
    args.name = "device-reboot";
    return esp_timer_create(&args, &reboot_timer) == ESP_OK;
}

struct UploadGuard {
    bool release = true;
    ~UploadGuard() {
        if (release)
            upload_active = false;
    }
};
} // namespace

bool reboot_available() {
    return create_reboot_timer() && !esp_timer_is_active(reboot_timer);
}

bool schedule_reboot() {
    if (!reboot_available())
        return false;
    return esp_timer_start_once(reboot_timer, 1500000) == ESP_OK;
}

bool schedule_factory_reset(bool erase_history) {
    bool expected = false;
    if (!reset_active.compare_exchange_strong(expected, true))
        return false;
    if (!reboot_available()) {
        reset_active = false;
        return false;
    }
    nvs_handle_t handle;
    if (nvs_open(reset_namespace, NVS_READWRITE, &handle) != ESP_OK) {
        reset_active = false;
        return false;
    }
    const auto mode = uint8_t(erase_history ? 2 : 1);
    auto result = nvs_set_u8(handle, reset_key, mode);
    if (result == ESP_OK)
        result = nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK || !schedule_reboot()) {
        if (nvs_open(reset_namespace, NVS_READWRITE, &handle) == ESP_OK) {
            nvs_erase_key(handle, reset_key);
            nvs_commit(handle);
            nvs_close(handle);
        }
        reset_active = false;
        return false;
    }
    return true;
}

esp_err_t apply_pending_factory_reset() {
    nvs_handle_t handle;
    auto result = nvs_open(reset_namespace, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (result != ESP_OK)
        return result;
    uint8_t mode = 0;
    result = nvs_get_u8(handle, reset_key, &mode);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (result != ESP_OK || (mode != 1 && mode != 2))
        return result == ESP_OK ? ESP_ERR_INVALID_STATE : result;

    // The intent is processed before history/network workers start, so no queued
    // writes can race an explicitly requested history erase.
    if (mode == 2) {
        const auto *history =
            esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "history");
        if (!history)
            return ESP_ERR_NOT_FOUND;
        result = esp_partition_erase_range(history, 0, history->size);
        if (result != ESP_OK)
            return result;
    }
    result = nvs_flash_erase();
    if (result != ESP_OK)
        return result;
    esp_restart();
    return ESP_OK;
}

OtaStatus ota_status() {
    OtaStatus status{};
    const auto *description = esp_app_get_description();
    if (description)
        status.version = description->version;
    const auto *running = esp_ota_get_running_partition();
    if (!running)
        return status;
    status.partition = running->label;
    esp_ota_img_states_t image_state{};
    if (esp_ota_get_state_partition(running, &image_state) != ESP_OK) {
        status.state = "unknown";
        return status;
    }
    switch (image_state) {
    case ESP_OTA_IMG_NEW:
        status.state = "new";
        break;
    case ESP_OTA_IMG_PENDING_VERIFY:
        status.state = "pending_verify";
        break;
    case ESP_OTA_IMG_VALID:
        status.state = "valid";
        break;
    case ESP_OTA_IMG_INVALID:
        status.state = "invalid";
        break;
    case ESP_OTA_IMG_ABORTED:
        status.state = "aborted";
        break;
    default:
        status.state = "unknown";
        break;
    }
    return status;
}

esp_err_t handle_ota_upload(httpd_req_t *request) {
    bool expected = false;
    if (!upload_active.compare_exchange_strong(expected, true))
        return response(request, "409 Conflict", "An update is already in progress");
    UploadGuard guard;

    if (request->content_len < static_cast<int>(sizeof(esp_image_header_t)))
        return response(request, "400 Bad Request", "Firmware image is empty or truncated");
    size_t content_type_length = httpd_req_get_hdr_value_len(request, "Content-Type");
    if (content_type_length != strlen("application/octet-stream"))
        return response(request, "415 Unsupported Media Type", "Expected application/octet-stream");
    std::array<char, sizeof("application/octet-stream")> content_type{};
    if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type.data(),
                                    content_type.size()) != ESP_OK ||
        strcmp(content_type.data(), "application/octet-stream") != 0)
        return response(request, "415 Unsupported Media Type", "Expected application/octet-stream");

    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition || static_cast<size_t>(request->content_len) > partition->size)
        return response(request, "413 Content Too Large", "Image exceeds the inactive OTA slot");
    if (!create_reboot_timer())
        return response(request, "503 Service Unavailable", "Could not schedule update restart");

    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(partition, request->content_len, &handle) != ESP_OK)
        return response(request, "503 Service Unavailable", "Could not start firmware update");

    std::array<uint8_t, 1024> chunk{};
    size_t received = 0;
    const int64_t upload_started_us = esp_timer_get_time();
    while (received < static_cast<size_t>(request->content_len)) {
        if (esp_timer_get_time() - upload_started_us > 120000000) {
            esp_ota_abort(handle);
            return response(request, "408 Request Timeout", "Firmware upload exceeded 120 seconds");
        }
        const size_t wanted =
            std::min(chunk.size(), static_cast<size_t>(request->content_len) - received);
        const int count = httpd_req_recv(request, reinterpret_cast<char *>(chunk.data()), wanted);
        if (count <= 0 || esp_ota_write(handle, chunk.data(), count) != ESP_OK) {
            esp_ota_abort(handle);
            return response(request, "400 Bad Request", "Firmware upload was interrupted");
        }
        received += static_cast<size_t>(count);
    }

    const esp_err_t finalized = esp_ota_end(handle);
    if (finalized != ESP_OK)
        return response(request, "422 Unprocessable Content", "Firmware image validation failed");
    if (esp_ota_set_boot_partition(partition) != ESP_OK)
        return response(request, "503 Service Unavailable",
                        "Could not select the new firmware slot");
    guard.release = false;

    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const char *accepted = "{\"status\":\"rebooting\"}";
    const esp_err_t sent = httpd_resp_send(request, accepted, HTTPD_RESP_USE_STRLEN);
    if (!schedule_reboot())
        esp_restart();
    return sent;
}
} // namespace monitor
