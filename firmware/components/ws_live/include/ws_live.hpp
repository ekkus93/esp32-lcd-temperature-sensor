#pragma once

#include "auth_service.hpp"
#include "esp_http_server.h"
#include <array>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace monitor {
class WsLive {
  public:
    // Live readings are public: a client without a session is admitted as a viewer. `capacity`
    // slots exist in total and `owner_reserve` of them are kept for a signed-in owner, so a crowd
    // of viewers can never lock the owner out.
    explicit WsLive(size_t capacity = 6, size_t owner_reserve = 1)
        : clients_(capacity), owner_reserve_(owner_reserve) {}
    void configure(httpd_handle_t server, AuthService &auth, std::function<std::string()> snapshot);
    // An empty `token` is an anonymous viewer; a non-empty one must be a valid session.
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
    std::vector<Client> clients_;
    size_t owner_reserve_;
    std::atomic<bool> pending_{false};
};
} // namespace monitor
