#include "ws_frame.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {
struct FakeSocket {
    std::vector<uint8_t> written;
    int calls = 0;
    int max_chunk = 200000;
    int error_at = -1;
    int error_number = EAGAIN;
};
FakeSocket *active = nullptr;

ssize_t send_stub(int fd, const void *data, size_t size, int flags) {
    assert(fd == 3);
    assert((flags & MSG_DONTWAIT) != 0);
    auto &socket = *active;
    const int call = socket.calls++;
    if (call == socket.error_at) {
        errno = socket.error_number;
        return -1;
    }
    const size_t n = std::min(size, static_cast<size_t>(socket.max_chunk));
    const auto *bytes = static_cast<const uint8_t *>(data);
    socket.written.insert(socket.written.end(), bytes, bytes + n);
    return static_cast<ssize_t>(n);
}

void send_success(size_t length, size_t header_length, uint8_t length_marker) {
    FakeSocket fake;
    active = &fake;
    const std::string payload(length, 'a');
    assert(monitor::ws_frame::send_text_nonblocking(3, payload, send_stub));
    assert(fake.written.size() == header_length + length);
    assert(fake.written[0] == 0x81 && fake.written[1] == length_marker);
    if (header_length == 4) {
        assert(fake.written[2] == uint8_t(length >> 8));
        assert(fake.written[3] == uint8_t(length));
    }
    if (header_length == 10) {
        for (int i = 0; i < 8; ++i)
            assert(fake.written[2 + i] == uint8_t(uint64_t(length) >> ((7 - i) * 8)));
    }
    for (size_t i = 0; i < length; ++i)
        assert(fake.written[header_length + i] == 'a');
}
} // namespace

int main() {
    send_success(0, 2, 0);
    send_success(125, 2, 125);
    send_success(126, 4, 126);
    send_success(65535, 4, 126);
    send_success(65536, 10, 127);

    // Both header and payload can be split across multiple successful writes.
    FakeSocket partial;
    partial.max_chunk = 1;
    active = &partial;
    assert(monitor::ws_frame::send_text_nonblocking(3, "hi", send_stub));
    assert((partial.written == std::vector<uint8_t>{0x81, 2, 'h', 'i'}));

    // An interrupted send is safe to retry within the shared operation budget.
    FakeSocket interrupted;
    interrupted.error_at = 0;
    interrupted.error_number = EINTR;
    active = &interrupted;
    assert(monitor::ws_frame::send_text_nonblocking(3, "ok", send_stub));

    // A full TCP buffer, broken socket, or excessive short sends must never
    // block the HTTP server or report a complete frame. The caller closes it.
    for (int error : {EAGAIN, EPIPE, ECONNRESET}) {
        FakeSocket failure;
        failure.error_at = 1; // Header sent, payload blocked or failed.
        failure.error_number = error;
        active = &failure;
        assert(!monitor::ws_frame::send_text_nonblocking(3, "hello", send_stub));
        assert(failure.calls == 2);
    }
    FakeSocket starvation;
    starvation.max_chunk = 1;
    active = &starvation;
    assert(!monitor::ws_frame::send_text_nonblocking(3, std::string(1000, 'x'), send_stub));
    assert(starvation.calls == 16);
    assert(!monitor::ws_frame::send_text_nonblocking(3, "hello", nullptr));

    // A real unconsumed socket must fail promptly despite kernel buffering.
    // This exercises MSG_DONTWAIT on POSIX, not ESP-IDF TCP or its HTTP stack.
    int sockets[2]{};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    int send_buffer = 4096;
    assert(setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer)) == 0);
    const std::string huge_frame(512 * 1024, 'x');
    assert(!monitor::ws_frame::send_text_nonblocking(sockets[0], huge_frame, send));
    close(sockets[0]);
    close(sockets[1]);

    // A peer may disconnect immediately before a scheduled broadcast.
    // The native sender must return failure instead of terminating the
    // firmware/host process with SIGPIPE (POSIX MSG_NOSIGNAL).
    int disconnected[2]{};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, disconnected) == 0);
    close(disconnected[1]);
    assert(!monitor::ws_frame::send_text_nonblocking(disconnected[0], "gone", send));
    close(disconnected[0]);
}
