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
#include <cerrno>
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

bool socket_send_all(int fd, const char *data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        const int count = send(fd, data + sent, length - sent, 0);
        if (count <= 0)
            return false;
        sent += count;
    }
    return true;
}

bool socket_receive_all(int fd, uint8_t *data, size_t length) {
    size_t received = 0;
    while (received < length) {
        const int count = recv(fd, data + received, length - received, 0);
        if (count <= 0)
            return false;
        received += count;
    }
    return true;
}

bool websocket_payload(int fd, std::string &payload) {
    uint8_t header[2]{};
    if (!socket_receive_all(fd, header, sizeof(header)) || (header[1] & 0x80))
        return false;
    uint64_t length = header[1] & 0x7f;
    if (length == 126) {
        uint8_t extended[2]{};
        if (!socket_receive_all(fd, extended, sizeof(extended)))
            return false;
        length = (uint64_t(extended[0]) << 8) | extended[1];
    } else if (length == 127) {
        uint8_t extended[8]{};
        if (!socket_receive_all(fd, extended, sizeof(extended)))
            return false;
        length = 0;
        for (auto byte : extended)
            length = (length << 8) | byte;
    }
    if (length > 8192)
        return false;
    payload.resize(length);
    if (length && !socket_receive_all(fd, reinterpret_cast<uint8_t *>(payload.data()), length))
        return false;
    return true;
}

int websocket_handshake(int fd, const char *origin, const char *cookie) {
    std::string request = "GET /api/v1/live HTTP/1.1\r\nHost: 192.168.4.1\r\nUpgrade: websocket\r\n"
                          "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                          "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nOrigin: ";
    request += origin;
    request += "\r\nCookie: ";
    request += cookie;
    request += "\r\n\r\n";
    if (!socket_send_all(fd, request.data(), request.size()))
        return 0;
    std::string response;
    char character = 0;
    while (response.size() < 2048 &&
           !(response.size() >= 4 && response.compare(response.size() - 4, 4, "\r\n\r\n") == 0)) {
        if (recv(fd, &character, 1, 0) != 1)
            return 0;
        response.push_back(character);
    }
    if (response.rfind("HTTP/1.1 101 ", 0) == 0)
        return 101;
    if (response.size() >= 12 && response.rfind("HTTP/1.1 ", 0) == 0)
        return std::atoi(response.c_str() + 9);
    return 0;
}

void websocket_load_bridge(cJSON *request) {
    auto *headers = cJSON_GetObjectItemCaseSensitive(request, "headers");
    auto *origin =
        cJSON_IsObject(headers) ? cJSON_GetObjectItemCaseSensitive(headers, "Origin") : nullptr;
    auto *cookie =
        cJSON_IsObject(headers) ? cJSON_GetObjectItemCaseSensitive(headers, "Cookie") : nullptr;
    unsigned wanted = 4;
    if (auto *clients = cJSON_GetObjectItemCaseSensitive(request, "clients");
        cJSON_IsNumber(clients))
        wanted = std::clamp(unsigned(clients->valueint), 1u, 4u);
    std::vector<int> sockets;
    sockets.reserve(wanted);
    int overflow_status = 0;
    int failure_status = 0;
    int failure_code = 0;
    int failure_errno = 0;
    const bool credentials_valid = cJSON_IsString(origin) && cJSON_IsString(cookie) &&
                                   std::strlen(origin->valuestring) < 256 &&
                                   std::strlen(cookie->valuestring) < 512;
    if (credentials_valid) {
        for (unsigned index = 0; index <= wanted; ++index) {
            int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (fd < 0) {
                failure_code = -1;
                failure_errno = errno;
                break;
            }
            struct timeval timeout {
                8, 0
            };
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(80);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
                failure_code = -2;
                failure_errno = errno;
                close(fd);
                break;
            }
            const int status = websocket_handshake(fd, origin->valuestring, cookie->valuestring);
            if (index < wanted && status == 101)
                sockets.push_back(fd);
            else {
                if (index == wanted)
                    overflow_status = status;
                else {
                    failure_status = status;
                    failure_code = -3;
                }
                close(fd);
                break;
            }
        }
    }

    auto *output = cJSON_CreateObject();
    auto *sequences = cJSON_AddArrayToObject(output, "sequences");
    bool frames_complete = sockets.size() == wanted;
    unsigned valid_frames = 0;
    for (int fd : sockets) {
        std::string payload;
        if (!websocket_payload(fd, payload)) {
            frames_complete = false;
            continue;
        }
        auto *frame = cJSON_ParseWithLength(payload.c_str(), payload.size());
        auto *sequence =
            cJSON_IsObject(frame) ? cJSON_GetObjectItemCaseSensitive(frame, "sequence") : nullptr;
        auto *version =
            cJSON_IsObject(frame) ? cJSON_GetObjectItemCaseSensitive(frame, "version") : nullptr;
        auto *sensors =
            cJSON_IsObject(frame) ? cJSON_GetObjectItemCaseSensitive(frame, "sensors") : nullptr;
        if (cJSON_IsNumber(sequence))
            cJSON_AddItemToArray(sequences, cJSON_CreateNumber(sequence->valuedouble));
        else
            frames_complete = false;
        if (cJSON_IsNumber(version) && version->valueint == 1 && cJSON_IsArray(sensors))
            ++valid_frames;
        else
            frames_complete = false;
        cJSON_Delete(frame);
    }

    int api_status = 0;
    if (frames_complete) {
        int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd >= 0) {
            struct timeval timeout {
                5, 0
            };
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(80);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0) {
                std::string get = "GET /api/v1/status HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: ";
                get += origin->valuestring;
                get += "\r\nCookie: ";
                get += cookie->valuestring;
                get += "\r\nConnection: close\r\n\r\n";
                if (socket_send_all(fd, get.data(), get.size())) {
                    char status_line[32]{};
                    size_t used = 0;
                    while (used + 1 < sizeof(status_line) &&
                           recv(fd, &status_line[used], 1, 0) == 1) {
                        if (status_line[used++] == '\n')
                            break;
                    }
                    if (std::strncmp(status_line, "HTTP/1.1 ", 9) == 0)
                        api_status = std::atoi(status_line + 9);
                }
            }
            close(fd);
        }
    }

    cJSON_AddNumberToObject(output, "accepted_clients", sockets.size());
    cJSON_AddNumberToObject(output, "overflow_status", overflow_status);
    cJSON_AddNumberToObject(output, "failure_status", failure_status);
    cJSON_AddNumberToObject(output, "failure_code", failure_code);
    cJSON_AddNumberToObject(output, "failure_errno", failure_errno);
    cJSON_AddNumberToObject(output, "valid_frames", valid_frames);
    cJSON_AddBoolToObject(output, "frames_complete", frames_complete);
    cJSON_AddNumberToObject(output, "api_status", api_status);
    for (int fd : sockets)
        close(fd);
    char *text = cJSON_PrintUnformatted(output);
    if (text) {
        std::printf("\nWS_LOAD_BRIDGE_BEGIN\n%s\nWS_LOAD_BRIDGE_END\n", text);
        cJSON_free(text);
    }
    cJSON_Delete(output);
}

void websocket_slow_bridge(cJSON *request) {
    auto *headers = cJSON_GetObjectItemCaseSensitive(request, "headers");
    auto *origin =
        cJSON_IsObject(headers) ? cJSON_GetObjectItemCaseSensitive(headers, "Origin") : nullptr;
    auto *cookie =
        cJSON_IsObject(headers) ? cJSON_GetObjectItemCaseSensitive(headers, "Cookie") : nullptr;
    int duration_seconds = 30;
    if (auto *duration = cJSON_GetObjectItemCaseSensitive(request, "duration_seconds");
        cJSON_IsNumber(duration))
        duration_seconds = std::clamp(duration->valueint, 5, 300);
    int fd = -1;
    bool upgraded = false;
    int receive_buffer_size = 0;
    if (cJSON_IsString(origin) && cJSON_IsString(cookie) &&
        std::strlen(origin->valuestring) < 256 && std::strlen(cookie->valuestring) < 512) {
        fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd >= 0) {
            int receive_buffer = 1;
            setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer));
            socklen_t receive_buffer_length = sizeof(receive_buffer_size);
            getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer_size, &receive_buffer_length);
            struct timeval timeout {
                8, 0
            };
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(80);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0)
                upgraded = websocket_handshake(fd, origin->valuestring, cookie->valuestring) == 101;
        }
    }

    unsigned http_successes = 0;
    int http_failures = 0;
    int64_t max_http_ms = 0;
    int api_fd = -1;
    if (upgraded) {
        for (int second = 0; second < duration_seconds; ++second) {
            int status = 0;
            const int64_t started = esp_timer_get_time() / 1000;
            if (api_fd < 0) {
                api_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (api_fd >= 0) {
                    struct timeval api_timeout {
                        5, 0
                    };
                    setsockopt(api_fd, SOL_SOCKET, SO_RCVTIMEO, &api_timeout, sizeof(api_timeout));
                    sockaddr_in address{};
                    address.sin_family = AF_INET;
                    address.sin_port = htons(80);
                    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    if (connect(api_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) !=
                        0) {
                        close(api_fd);
                        api_fd = -1;
                    }
                }
            }
            if (api_fd >= 0) {
                std::string get = "GET /api/v1/status HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: ";
                get += origin->valuestring;
                get += "\r\nCookie: ";
                get += cookie->valuestring;
                get += "\r\nConnection: keep-alive\r\n\r\n";
                if (socket_send_all(api_fd, get.data(), get.size())) {
                    std::string response;
                    char character = 0;
                    while (response.size() < 4096 &&
                           !(response.size() >= 4 &&
                             response.compare(response.size() - 4, 4, "\r\n\r\n") == 0)) {
                        if (recv(api_fd, &character, 1, 0) != 1)
                            break;
                        response.push_back(character);
                    }
                    if (response.rfind("HTTP/1.1 ", 0) == 0) {
                        status = std::atoi(response.c_str() + 9);
                        const char *length_header = strcasestr(response.c_str(), "Content-Length:");
                        size_t remaining =
                            length_header ? std::strtoul(length_header + 15, nullptr, 10) : 0;
                        char body[256];
                        while (remaining) {
                            const size_t chunk = std::min(remaining, sizeof(body));
                            const int received = recv(api_fd, body, chunk, 0);
                            if (received <= 0) {
                                status = 0;
                                close(api_fd);
                                api_fd = -1;
                                break;
                            }
                            remaining -= size_t(received);
                        }
                    } else {
                        close(api_fd);
                        api_fd = -1;
                    }
                } else {
                    close(api_fd);
                    api_fd = -1;
                }
            }
            max_http_ms = std::max(max_http_ms, esp_timer_get_time() / 1000 - started);
            if (status == 200)
                ++http_successes;
            else
                ++http_failures;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    if (api_fd >= 0)
        close(api_fd);

    size_t drained = 0;
    bool peer_closed = false;
    if (fd >= 0 && upgraded) {
        struct timeval drain_timeout {
            0, 100000
        };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &drain_timeout, sizeof(drain_timeout));
        char buffer[512];
        const int64_t close_deadline = esp_timer_get_time() + 5000000;
        while (drained < 65536 && esp_timer_get_time() < close_deadline) {
            const int received = recv(fd, buffer, sizeof(buffer), 0);
            if (received > 0)
                drained += size_t(received);
            else if (received == 0) {
                peer_closed = true;
                break;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                break;
        }
    }
    if (fd >= 0)
        close(fd);

    auto *output = cJSON_CreateObject();
    cJSON_AddBoolToObject(output, "upgraded", upgraded);
    cJSON_AddBoolToObject(output, "peer_closed", peer_closed);
    cJSON_AddNumberToObject(output, "http_successes", http_successes);
    cJSON_AddNumberToObject(output, "http_failures", http_failures);
    cJSON_AddNumberToObject(output, "max_http_ms", max_http_ms);
    cJSON_AddNumberToObject(output, "drained_bytes", drained);
    cJSON_AddNumberToObject(output, "receive_buffer_size", receive_buffer_size);
    char *text = cJSON_PrintUnformatted(output);
    if (text) {
        std::printf("\nWS_SLOW_BRIDGE_BEGIN\n%s\nWS_SLOW_BRIDGE_END\n", text);
        cJSON_free(text);
    }
    cJSON_Delete(output);
}

void websocket_bridge(cJSON *request) {
    auto *headers = cJSON_GetObjectItemCaseSensitive(request, "headers");
    auto *origin =
        cJSON_IsObject(headers) ? cJSON_GetObjectItemCaseSensitive(headers, "Origin") : nullptr;
    auto *cookie =
        cJSON_IsObject(headers) ? cJSON_GetObjectItemCaseSensitive(headers, "Cookie") : nullptr;
    auto *frame_count = cJSON_GetObjectItemCaseSensitive(request, "frames");
    auto *send_text = cJSON_GetObjectItemCaseSensitive(request, "send_text");
    const unsigned wanted =
        cJSON_IsNumber(frame_count) ? std::clamp(unsigned(frame_count->valueint), 1u, 3u) : 2u;
    cJSON *output = cJSON_CreateObject();
    auto *frames = cJSON_AddArrayToObject(output, "frames");
    auto *received_ms = cJSON_AddArrayToObject(output, "received_ms");
    bool connected = false;
    bool valid_upgrade = false;
    bool complete = true;
    bool sent_text = false;
    bool socket_closed = false;
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd >= 0) {
        struct timeval timeout {
            8, 0
        };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(80);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        connected = connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
        if (connected && cJSON_IsString(origin) && cJSON_IsString(cookie) &&
            std::strlen(origin->valuestring) < 256 && std::strlen(cookie->valuestring) < 512) {
            std::string handshake =
                "GET /api/v1/live HTTP/1.1\r\nHost: 192.168.4.1\r\n"
                "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
            handshake += "Origin: ";
            handshake += origin->valuestring;
            handshake += "\r\nCookie: ";
            handshake += cookie->valuestring;
            handshake += "\r\n\r\n";
            if (socket_send_all(fd, handshake.data(), handshake.size())) {
                std::string response;
                char character = 0;
                while (response.size() < 2048 &&
                       !(response.size() >= 4 &&
                         response.compare(response.size() - 4, 4, "\r\n\r\n") == 0)) {
                    if (recv(fd, &character, 1, 0) != 1)
                        break;
                    response.push_back(character);
                }
                valid_upgrade = response.rfind("HTTP/1.1 101 ", 0) == 0 && response.size() < 2048 &&
                                response.size() >= 4 &&
                                response.compare(response.size() - 4, 4, "\r\n\r\n") == 0;
                if (valid_upgrade) {
                    if (cJSON_IsString(send_text) && std::strlen(send_text->valuestring) <= 1024) {
                        const size_t length = std::strlen(send_text->valuestring);
                        std::array<uint8_t, 1036> frame{};
                        size_t offset = 0;
                        frame[offset++] = 0x81; // FIN + text
                        if (length < 126)
                            frame[offset++] = uint8_t(0x80 | length);
                        else {
                            frame[offset++] = 0xfe;
                            frame[offset++] = uint8_t(length >> 8);
                            frame[offset++] = uint8_t(length);
                        }
                        uint32_t mask = esp_random();
                        uint8_t mask_key[4]{};
                        for (unsigned index = 0; index < 4; ++index) {
                            mask_key[index] = uint8_t(mask >> (index * 8));
                            frame[offset++] = mask_key[index];
                        }
                        for (size_t index = 0; index < length; ++index)
                            frame[offset++] =
                                uint8_t(send_text->valuestring[index]) ^ mask_key[index % 4];
                        sent_text =
                            socket_send_all(fd, reinterpret_cast<char *>(frame.data()), offset);
                    }
                    for (unsigned index = 0; index < wanted; ++index) {
                        std::string payload;
                        if (!websocket_payload(fd, payload)) {
                            complete = false;
                            socket_closed = true;
                            break;
                        }
                        cJSON_AddItemToArray(frames, cJSON_CreateString(payload.c_str()));
                        cJSON_AddItemToArray(
                            received_ms, cJSON_CreateNumber(double(esp_timer_get_time() / 1000)));
                    }
                }
            }
        }
        close(fd);
    }
    cJSON_AddBoolToObject(output, "connected", connected);
    cJSON_AddBoolToObject(output, "upgraded", valid_upgrade);
    cJSON_AddBoolToObject(output, "complete", complete);
    cJSON_AddBoolToObject(output, "sent_text", sent_text);
    cJSON_AddBoolToObject(output, "socket_closed", socket_closed);
    char *text = cJSON_PrintUnformatted(output);
    if (text) {
        std::printf("\nWS_BRIDGE_BEGIN\n%s\nWS_BRIDGE_END\n", text);
        cJSON_free(text);
    }
    cJSON_Delete(output);
}

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
    if (uri.size() > CONFIG_HTTPD_MAX_URI_LEN || uri.find_first_of("\r\n") != std::string::npos)
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
        } else if (std::strncmp(line, "WS_BRIDGE ", 10) == 0) {
            cJSON *request = cJSON_Parse(line + 10);
            if (request) {
                websocket_bridge(request);
                cJSON_Delete(request);
            }
        } else if (std::strncmp(line, "WS_LOAD_BRIDGE ", 15) == 0) {
            cJSON *request = cJSON_Parse(line + 15);
            if (request) {
                websocket_load_bridge(request);
                cJSON_Delete(request);
            }
        } else if (std::strncmp(line, "WS_SLOW_BRIDGE ", 15) == 0) {
            cJSON *request = cJSON_Parse(line + 15);
            if (request) {
                websocket_slow_bridge(request);
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
