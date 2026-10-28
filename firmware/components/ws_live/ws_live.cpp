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
    // The HTTP pre-handshake authenticated this token, but the session can
    // expire or be revoked before admission runs. Never reserve a slot for
    // an invalid session, even when a slot is currently available.
    if (!server_ || !auth_ || fd < 0 || !auth_->authorize(token, esp_timer_get_time() / 1000))
        return false;
    Client *slot = nullptr;
    for (auto &client : clients_) {
        if (client.fd == fd) {
            if (client.quarantined) {
                // A prior partial send poisoned this stream. Refuse reuse
                // until the HTTP server has actually closed the socket.
                if (httpd_sess_trigger_close(server_, fd) == ESP_OK)
                    client = Client{};
                return false;
            }
            slot = &client;
            break;
        }
        if (!slot && (client.fd < 0 ||
                      httpd_ws_get_fd_info(server_, client.fd) != HTTPD_WS_CLIENT_WEBSOCKET))
            slot = &client;
    }
    if (!slot && auth_) {
        // An expired or logged-out administrator session must not reserve a
        // live-client slot until the next periodic broadcast. Close it before
        // admitting a newly authenticated subscriber.
        const auto now_ms = esp_timer_get_time() / 1000;
        for (auto &client : clients_) {
            if (auth_->authorize(client.token, now_ms))
                continue;
            // Do not overwrite a still-open unauthorized session if the
            // HTTP server refuses the close request. Retry on next admission.
            if (httpd_sess_trigger_close(server_, client.fd) != ESP_OK)
                continue;
            slot = &client;
            break;
        }
    }
    if (!slot)
        return false;
    slot->fd = fd;
    slot->token = token;
    slot->quarantined = false;
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
    // Keep the historical behavior for empty RFC 6455 control heartbeats.
    // Data frames, including empty text/binary commands, are never allowed.
    const bool is_heartbeat = frame.type == HTTPD_WS_TYPE_PING || frame.type == HTTPD_WS_TYPE_PONG;
    if (frame.len == 0 && is_heartbeat)
        return ESP_OK;
    // A refused close must not make a forbidden client frame look accepted.
    // Returning ESP_FAIL lets the HTTP server terminate the request/socket.
    return httpd_sess_trigger_close(server_, fd) == ESP_OK ? ESP_OK : ESP_FAIL;
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
    // An empty snapshot suppresses data frames, not session maintenance.
    // Otherwise failed-close quarantine and expired sessions can linger
    // indefinitely while the producer has no data to publish.
    // Only registered live sessions need a broadcast. Enumerating *all*
    // HTTP sockets into a fixed-size array fails when unrelated REST/asset
    // clients outnumber the array, silently starving every live subscriber.
    const auto now_ms = esp_timer_get_time() / 1000;
    for (auto &client : self.clients_) {
        const int fd = client.fd;
        // The HTTP server may have closed/reused a registered descriptor.
        // Never send a WebSocket frame to a non-WebSocket connection.
        if (fd < 0 || httpd_ws_get_fd_info(self.server_, fd) != HTTPD_WS_CLIENT_WEBSOCKET)
            continue;
        if (client.quarantined) {
            // A previous nonblocking send failed and close was refused.
            // Retrying a frame could corrupt the RFC 6455 stream.
            if (httpd_sess_trigger_close(self.server_, fd) == ESP_OK)
                client = Client{};
            continue;
        }
        if (!self.auth_->authorize(client.token, now_ms)) {
            httpd_sess_trigger_close(self.server_, fd);
            continue;
        }
        if (text.empty())
            continue;
        if (!ws_frame::send_text_nonblocking(fd, text, send)) {
            client.quarantined = true;
            ESP_LOGW("ws_live", "Closing slow or failed WebSocket send for fd=%d", fd);
            if (httpd_sess_trigger_close(self.server_, fd) == ESP_OK)
                client = Client{};
        }
    }
}
} // namespace monitor
