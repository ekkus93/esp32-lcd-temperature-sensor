#include "ws_live.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "ws_frame.hpp"
#include <sys/socket.h>

namespace monitor {
void WsLive::configure(httpd_handle_t server, AuthService &auth,
                       std::function<std::string()> snapshot) {
    server_ = server;
    auth_ = &auth;
    snapshot_ = std::move(snapshot);
}

bool WsLive::add_client(int fd, const std::string &token) {
    Client *slot = nullptr;
    for (auto &client : clients_) {
        if (client.fd == fd) {
            slot = &client;
            break;
        }
        if (!slot && (client.fd < 0 ||
                      httpd_ws_get_fd_info(server_, client.fd) != HTTPD_WS_CLIENT_WEBSOCKET))
            slot = &client;
    }
    if (!slot)
        return false;
    slot->fd = fd;
    slot->token = token;
    return true;
}

bool WsLive::schedule() {
    if (!server_ || pending_.exchange(true))
        return true;
    if (httpd_queue_work(server_, broadcast, this) == ESP_OK)
        return true;
    pending_ = false;
    return false;
}

esp_err_t WsLive::receive(httpd_req_t *request) {
    const int fd = httpd_req_to_sockfd(request);
    httpd_ws_frame_t frame{};
    const auto result = httpd_ws_recv_frame(request, &frame, 0);
    if (result != ESP_OK || frame.len > 1024) {
        httpd_sess_trigger_close(server_, fd);
        return ESP_FAIL;
    }
    // The live channel is server-to-client only; unexpected commands are closed.
    if (frame.type == HTTPD_WS_TYPE_CLOSE || frame.len) {
        httpd_sess_trigger_close(server_, fd);
        return ESP_OK;
    }
    return ESP_OK;
}

void WsLive::broadcast(void *context) {
    auto &self = *static_cast<WsLive *>(context);
    struct ClearPending {
        std::atomic<bool> &flag;
        ~ClearPending() {
            flag = false;
        }
    } clear{self.pending_};

    const auto text = self.snapshot_ ? self.snapshot_() : std::string{};
    if (text.empty())
        return;
    size_t count = 6;
    int sockets[6]{};
    if (httpd_get_client_list(self.server_, &count, sockets) != ESP_OK)
        return;
    const auto now_ms = esp_timer_get_time() / 1000;
    for (size_t i = 0; i < count; ++i) {
        const int fd = sockets[i];
        if (httpd_ws_get_fd_info(self.server_, fd) != HTTPD_WS_CLIENT_WEBSOCKET)
            continue;
        auto found = self.clients_.begin();
        while (found != self.clients_.end() && found->fd != fd)
            ++found;
        if (found == self.clients_.end() || !self.auth_->authorize(found->token, now_ms)) {
            httpd_sess_trigger_close(self.server_, fd);
            continue;
        }
        if (!ws_frame::send_text_nonblocking(fd, text, send)) {
            ESP_LOGW("ws_live", "Closing slow or failed WebSocket send for fd=%d", fd);
            httpd_sess_trigger_close(self.server_, fd);
        }
    }
}
} // namespace monitor
