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
bool fail_close = false;
int64_t fake_now_us = 1000000;

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
    if (fail_close)
        return ESP_FAIL;
    closed_fds.push_back(fd);
    websocket_fds.erase(std::remove(websocket_fds.begin(), websocket_fds.end(), fd),
                        websocket_fds.end());
    return ESP_OK;
}
int64_t esp_timer_get_time() {
    return fake_now_us;
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
    // An expired or revoked token must not consume a live slot even if
    // the HTTP handshake previously authorized the request.
    assert(!live.add_client(peers[0][0], "expired"));
    for (size_t i = 0; i < 4; ++i) {
        add_websocket(peers[i][0]);
        assert(live.add_client(peers[i][0], "valid"));
    }
    add_websocket(peers[4][0]);
    assert(!live.add_client(peers[4][0], "valid")); // Four authorized slots maximum.

    // Start with four valid slots to exercise the admission cap. Then
    // expire one session before broadcasting to test periodic eviction.
    assert(live.add_client(peers[1][0], "expiring"));
    fake_now_us = 2000000; // The accepted session expires before broadcast.

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

    // If the HTTP server refuses the close request, a forbidden data frame
    // must fail the handler rather than leave the connection accepted.
    fail_inbound_read = false;
    inbound_type = 1;
    inbound_length = 0;
    fail_close = true;
    closed_fds.clear();
    assert(live.receive(&req) == ESP_FAIL);
    assert(!was_closed(req.fd));
    fail_close = false;

    // An expired administrator token must not monopolize one of four live
    // slots between five-second broadcasts. A fifth valid subscriber can
    // replace the expired one immediately, without evicting healthy peers.
    fake_now_us = 1000000;
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
    assert(admission.add_client(admission_peers[1][0], "expiring"));
    fake_now_us = 2000000;
    closed_fds.clear();
    fail_close = true;
    assert(!admission.add_client(admission_peers[4][0], "valid"));
    assert(!was_closed(admission_peers[1][0]));
    // A failed eviction must never leak a snapshot to the expired client,
    // and must not starve the other authorized subscribers.
    assert(admission.schedule());
    for (size_t i : {size_t(0), size_t(2), size_t(3)})
        drain(admission_peers[i][1], payload);
    char unexpected = 0;
    assert(recv(admission_peers[1][1], &unexpected, 1, MSG_DONTWAIT) == -1);
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    fail_close = false;
    assert(admission.add_client(admission_peers[4][0], "valid"));
    assert(was_closed(admission_peers[1][0]));
    for (size_t i : {size_t(0), size_t(2), size_t(3), size_t(4)})
        assert(!was_closed(admission_peers[i][0]));
    // A failed nonblocking send can leave a partial RFC 6455 frame. If
    // httpd refuses to close the socket, quarantine it: even after TCP
    // backpressure clears, never send another frame on that stream.
    monitor::WsLive quarantine;
    quarantine.configure(reinterpret_cast<httpd_handle_t>(1), auth, [&] { return payload; });
    std::array<std::array<int, 2>, 2> quarantine_peers{};
    for (auto &pair : quarantine_peers) {
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair.data()) == 0);
        add_websocket(pair[0]);
        assert(quarantine.add_client(pair[0], "valid"));
    }
    while (send(quarantine_peers[0][0], fill.data(), fill.size(), MSG_DONTWAIT) > 0) {
    }
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    closed_fds.clear();
    fail_close = true;
    assert(quarantine.schedule());
    assert(!was_closed(quarantine_peers[0][0]));
    drain(quarantine_peers[1][1], payload);
    std::array<char, 8192> scratch{};
    ssize_t received = 0;
    size_t drained_bytes = 0;
    while (true) {
        received = recv(quarantine_peers[0][1], scratch.data(), scratch.size(), MSG_DONTWAIT);
        if (received <= 0)
            break;
        drained_bytes += static_cast<size_t>(received);
    }
    assert(drained_bytes > 0);
    assert(received == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
    assert(quarantine.schedule());
    assert(!was_closed(quarantine_peers[0][0]));
    assert(recv(quarantine_peers[0][1], scratch.data(), scratch.size(), MSG_DONTWAIT) == -1);
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    drain(quarantine_peers[1][1], payload);
    assert(!quarantine.add_client(quarantine_peers[0][0], "valid"));
    // Even if the snapshot producer has no data, a quarantined stream
    // must be closed once the HTTP server allows it. Healthy subscribers
    // receive no empty WebSocket data frame.
    fail_close = false;
    std::string dynamic_payload;
    quarantine.configure(reinterpret_cast<httpd_handle_t>(1), auth,
                         [&] { return dynamic_payload; });
    assert(quarantine.schedule());
    assert(was_closed(quarantine_peers[0][0]));
    assert(recv(quarantine_peers[1][1], scratch.data(), scratch.size(), MSG_DONTWAIT) == -1);
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    dynamic_payload = payload;
    assert(quarantine.schedule());
    drain(quarantine_peers[1][1], payload);

    // Expired sessions must also be evicted without waiting for the first
    // nonempty snapshot, while an authorized subscriber remains connected.
    fake_now_us = 1000000;
    monitor::WsLive empty_snapshot;
    empty_snapshot.configure(reinterpret_cast<httpd_handle_t>(1), auth,
                             [&] { return dynamic_payload; });
    std::array<std::array<int, 2>, 2> empty_peers{};
    for (auto &pair : empty_peers) {
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair.data()) == 0);
        add_websocket(pair[0]);
    }
    assert(empty_snapshot.add_client(empty_peers[0][0], "expiring"));
    assert(empty_snapshot.add_client(empty_peers[1][0], "valid"));
    fake_now_us = 2000000;
    dynamic_payload.clear();
    closed_fds.clear();
    assert(empty_snapshot.schedule());
    assert(was_closed(empty_peers[0][0]));
    assert(!was_closed(empty_peers[1][0]));
    assert(recv(empty_peers[1][1], scratch.data(), scratch.size(), MSG_DONTWAIT) == -1);
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    dynamic_payload = payload;
    assert(empty_snapshot.schedule());
    drain(empty_peers[1][1], payload);
    for (auto &pair : empty_peers) {
        close(pair[0]);
        close(pair[1]);
    }
    for (auto &pair : quarantine_peers) {
        close(pair[0]);
        close(pair[1]);
    }

    for (auto &pair : admission_peers) {
        close(pair[0]);
        close(pair[1]);
    }

    for (auto &pair : peers) {
        close(pair[0]);
        close(pair[1]);
    }
}
