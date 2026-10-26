#include "captive_service.hpp"
#include "captive_dns.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <array>
#include <unistd.h>
namespace monitor {
namespace {
void worker(void *context) {
    auto &wifi = *static_cast<WifiService *>(context);
    int socket_fd = -1;
    while (true) {
        if (!wifi.info().provisioning) {
            if (socket_fd >= 0) {
                close(socket_fd);
                socket_fd = -1;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (socket_fd < 0) {
            socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (socket_fd < 0) {
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            struct timeval timeout {
                0, 250000
            };
            setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_port = htons(53);
            local.sin_addr.s_addr = inet_addr("192.168.4.1"); // AP interface only
            if (bind(socket_fd, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0) {
                close(socket_fd);
                socket_fd = -1;
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
        }
        std::array<uint8_t, 513> input{};
        sockaddr_in peer{};
        socklen_t peer_size = sizeof(peer);
        int count = recvfrom(socket_fd, input.data(), input.size(), 0,
                             reinterpret_cast<sockaddr *>(&peer), &peer_size);
        if (count <= 0 || count > 512 || !wifi.info().provisioning)
            continue;
        auto answer = captive_dns_reply(std::vector<uint8_t>(input.begin(), input.begin() + count));
        if (answer)
            sendto(socket_fd, answer->data(), answer->size(), 0,
                   reinterpret_cast<sockaddr *>(&peer), peer_size);
    }
}
} // namespace
bool start_captive_dns(WifiService &wifi) {
    return xTaskCreate(worker, "captive_dns", 4096, &wifi, 1, nullptr) == pdPASS;
}
} // namespace monitor
