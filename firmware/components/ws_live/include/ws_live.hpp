#pragma once

#include "auth_service.hpp"
#include "esp_http_server.h"
#include <array>
#include <atomic>
#include <functional>
#include <string>

namespace monitor {
class WsLive {
  public:
    void configure(httpd_handle_t server, AuthService &auth, std::function<std::string()> snapshot);
    bool add_client(int fd, const std::string &token);
    bool schedule();
    esp_err_t receive(httpd_req_t *request);

  private:
    static void broadcast(void *context);
    struct Client {
        int fd = -1;
        std::string token;
        // A failed partial send may leave a malformed frame on the wire.
        // Do not write again until the HTTP server closes this connection.
        bool quarantined = false;
    };
    httpd_handle_t server_ = nullptr;
    AuthService *auth_ = nullptr;
    std::function<std::string()> snapshot_;
    std::array<Client, 4> clients_{};
    std::atomic<bool> pending_{false};
};
} // namespace monitor
