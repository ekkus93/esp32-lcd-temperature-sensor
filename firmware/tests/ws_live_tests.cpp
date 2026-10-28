#include "ws_live.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {
std::vector<int> websocket_fds;
std::vector<int> http_fds;
std::vector<int> closed_fds;
int queued_count = 0;
bool defer_work = false;
bool reject_work = false;
void (*queued_fn)(void *) = nullptr;
void *queued_arg = nullptr;
size_t inbound_length = 0;
int inbound_type = 1;
bool fail_inbound_read = false;

void add_websocket(int fd) {
    websocket_fds.push_back(fd);
}
bool was_closed(int fd) {
    return std::find(closed_fds.begin(), closed_fds.end(), fd) != closed_fds.end();
}
void drain(int fd, const std::string &payload) {
    std::vector<char> bytes(payload.size() + 2);
    const auto received = recv(fd, bytes.data(), bytes.size(), MSG_DONTWAIT);
    assert(received == static_cast<ssize_t>(bytes.size()));
    assert(static_cast<uint8_t>(bytes[0]) == 0x81);
    assert(static_cast<uint8_t>(bytes[1]) == payload.size());
    assert(std::string(bytes.data() + 2, payload.size()) == payload);
}
} // namespace

int httpd_req_to_sockfd(httpd_req_t *request) {
    return request->fd;
}
esp_err_t httpd_ws_recv_frame(httpd_req_t *, httpd_ws_frame_t *frame, size_t) {
    frame->len = inbound_length;
    frame->type = inbound_type;
    return fail_inbound_read ? ESP_FAIL : ESP_OK;
}
int httpd_ws_get_fd_info(httpd_handle_t, int fd) {
    return std::find(websocket_fds.begin(), websocket_fds.end(), fd) != websocket_fds.end()
               ? HTTPD_WS_CLIENT_WEBSOCKET
               : HTTPD_WS_CLIENT_INVALID;
}
esp_err_t httpd_get_client_list(httpd_handle_t, size_t *count, int *fds) {
    const size_t total = websocket_fds.size() + http_fds.size();
    if (*count < total)
        return ESP_FAIL;
    *count = total;
    size_t i = 0;
    for (int fd : websocket_fds)
        fds[i++] = fd;
    for (int fd : http_fds)
        fds[i++] = fd;
    return ESP_OK;
}
esp_err_t httpd_queue_work(httpd_handle_t, void (*fn)(void *), void *arg) {
    ++queued_count;
    if (reject_work)
        return ESP_FAIL;
    if (defer_work) {
        queued_fn = fn;
        queued_arg = arg;
    } else {
        fn(arg);
    }
    return ESP_OK;
}
esp_err_t httpd_sess_trigger_close(httpd_handle_t, int fd) {
    closed_fds.push_back(fd);
    websocket_fds.erase(std::remove(websocket_fds.begin(), websocket_fds.end(), fd),
                        websocket_fds.end());
    return ESP_OK;
}
int64_t esp_timer_get_time() {
    return 1000000;
}

int main() {
    monitor::WsLive live;
    assert(live.schedule()); // Unconfigured instance does not enqueue work.
    assert(queued_count == 0);
    monitor::AuthService auth;
    const std::string payload = "{\"ok\":true}";
    live.configure(reinterpret_cast<httpd_handle_t>(1), auth, [&] { return payload; });

    std::array<std::array<int, 2>, 5> peers{};
    for (auto &pair : peers)
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair.data()) == 0);
    for (size_t i = 0; i < 4; ++i) {
        add_websocket(peers[i][0]);
        assert(live.add_client(peers[i][0], i == 1 ? "expired" : "valid"));
    }
    add_websocket(peers[4][0]);
    assert(!live.add_client(peers[4][0], "valid")); // Four authorized slots maximum.

    // A non-reading peer must be evicted without starving other clients.
    // Fill its kernel send queue before the broadcast; no ESP32 is needed.
    const std::string fill(4096, 'z');
    while (send(peers[0][0], fill.data(), fill.size(), MSG_DONTWAIT) > 0) {
    }
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    http_fds.push_back(peers[4][1]); // A concurrent non-WebSocket HTTP client.
    assert(live.schedule());
    assert(was_closed(peers[0][0])); // TCP backpressure.
    assert(was_closed(peers[1][0])); // Invalid/expired authorization.
    // A fifth WebSocket must be rejected at the upgrade gate (add_client),
    // not addressed by broadcasts; the mock never completes its upgrade.
    assert(!was_closed(peers[2][0]) && !was_closed(peers[3][0]));
    drain(peers[2][1], payload);
    drain(peers[3][1], payload);

    // More than six simultaneous HTTP/asset sessions used to make
    // httpd_get_client_list fail and silently skip *all* live broadcasts.
    // The production broadcaster must continue delivering to registered
    // live sessions without enumerating unrelated HTTP clients.
    for (int fd = 100; fd < 112; ++fd)
        http_fds.push_back(fd);
    const int before_many_http = queued_count;
    assert(live.schedule());
    assert(queued_count == before_many_http + 1);
    assert(!was_closed(peers[2][0]) && !was_closed(peers[3][0]));
    drain(peers[2][1], payload);
    drain(peers[3][1], payload);

    // Closed slots can be reused; a queued broadcast is coalesced and its
    // pending flag clears even when work is deferred to the HTTP task.
    add_websocket(peers[0][0]);
    assert(live.add_client(peers[0][0], "valid"));
    defer_work = true;
    const int before = queued_count;
    assert(live.schedule());
    assert(live.schedule());
    assert(queued_count == before + 1);
    assert(queued_fn);
    queued_fn(queued_arg);
    queued_fn = nullptr;
    defer_work = false;
    assert(live.schedule());
    assert(queued_count == before + 2);
    drain(peers[2][1], payload);
    drain(peers[3][1], payload);

    // A failed work enqueue must release pending state for the next attempt.
    reject_work = true;
    assert(!live.schedule());
    reject_work = false;
    assert(live.schedule());
    drain(peers[2][1], payload);
    drain(peers[3][1], payload);

    // Peer disappearance during a queued broadcast must close only that
    // WebSocket and leave healthy subscribers and HTTP connections intact.
    int disconnected[2]{};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, disconnected) == 0);
    add_websocket(disconnected[0]);
    assert(live.add_client(disconnected[0], "valid"));
    close(disconnected[1]);
    closed_fds.clear();
    assert(live.schedule());
    assert(was_closed(disconnected[0]));
    assert(!was_closed(peers[2][0]) && !was_closed(peers[3][0]));
    drain(peers[2][1], payload);
    drain(peers[3][1], payload);
    close(disconnected[0]);

    // Even an empty text/binary or control frame is unsolicited input on
    // this server-to-client-only endpoint and must close the session.
    httpd_req_t req{peers[2][0]};
    for (int type : {1, 2, 8}) {
        inbound_type = type;
        inbound_length = 0;
        closed_fds.clear();
        assert(live.receive(&req) == ESP_OK);
        assert(was_closed(req.fd));
    }
    // Empty Ping/Pong control messages remain allowed as before, while
    // nonempty unexpected control messages are still rejected.
    for (int type : {HTTPD_WS_TYPE_PING, HTTPD_WS_TYPE_PONG}) {
        inbound_type = type;
        inbound_length = 0;
        closed_fds.clear();
        assert(live.receive(&req) == ESP_OK);
        assert(!was_closed(req.fd));
        inbound_length = 1;
        assert(live.receive(&req) == ESP_OK);
        assert(was_closed(req.fd));
    }
    inbound_type = 1;
    // The allowed header length bound does not permit client writes.
    for (size_t length : {size_t(1), size_t(1024)}) {
        inbound_length = length;
        closed_fds.clear();
        assert(live.receive(&req) == ESP_OK);
        assert(was_closed(req.fd));
    }
    inbound_length = 1025;
    closed_fds.clear();
    assert(live.receive(&req) == ESP_FAIL);
    assert(was_closed(req.fd));
    fail_inbound_read = true;
    inbound_length = 0;
    closed_fds.clear();
    assert(live.receive(&req) == ESP_FAIL);
    assert(was_closed(req.fd));

    // An expired administrator token must not monopolize one of four live
    // slots between five-second broadcasts. A fifth valid subscriber can
    // replace the expired one immediately, without evicting healthy peers.
    monitor::WsLive admission;
    admission.configure(reinterpret_cast<httpd_handle_t>(1), auth, [&] { return payload; });
    std::array<std::array<int, 2>, 5> admission_peers{};
    for (auto &pair : admission_peers)
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair.data()) == 0);
    for (size_t i = 0; i < 5; ++i)
        add_websocket(admission_peers[i][0]);
    for (size_t i = 0; i < 4; ++i)
        assert(admission.add_client(admission_peers[i][0], "valid"));
    assert(!admission.add_client(admission_peers[4][0], "valid"));
    assert(admission.add_client(admission_peers[1][0], "expired"));
    closed_fds.clear();
    assert(admission.add_client(admission_peers[4][0], "valid"));
    assert(was_closed(admission_peers[1][0]));
    for (size_t i : {size_t(0), size_t(2), size_t(3), size_t(4)})
        assert(!was_closed(admission_peers[i][0]));
    for (auto &pair : admission_peers) {
        close(pair[0]);
        close(pair[1]);
    }

    for (auto &pair : peers) {
        close(pair[0]);
        close(pair[1]);
    }
}
