#include "idf_exchange.hpp"
#include "esp_timer.h"
#include "login_throttle.hpp"
#include "lwip/sockets.h"
#include <atomic>
#include <cerrno>
#include <sys/select.h>
#include <sys/socket.h>
#include <vector>
namespace monitor {
namespace {
// The receive deadline for a request body: the HTTP server reads through these while one is armed.
std::atomic<int> body_deadline_socket{-1};
std::atomic<int64_t> body_deadline_us{0};
int plain_recv(httpd_handle_t, int socket, char *buffer, size_t length, int flags) {
    return recv(socket, buffer, length, flags);
}
int deadline_recv(httpd_handle_t, int socket, char *buffer, size_t length, int flags) {
    const auto deadline = body_deadline_us.load();
    if (socket != body_deadline_socket.load() || deadline == 0)
        return recv(socket, buffer, length, flags);
    int selected;
    for (;;) {
        const int64_t remaining_us = deadline - esp_timer_get_time();
        if (remaining_us <= 0)
            return HTTPD_SOCK_ERR_TIMEOUT;
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(socket, &readable);
        timeval remaining{static_cast<time_t>(remaining_us / 1000000),
                          static_cast<suseconds_t>(remaining_us % 1000000)};
        selected = select(socket + 1, &readable, nullptr, nullptr, &remaining);
        if (selected >= 0 || errno != EINTR)
            break;
    }
    if (selected == 0)
        return HTTPD_SOCK_ERR_TIMEOUT;
    if (selected < 0)
        return HTTPD_SOCK_ERR_FAIL;
    return recv(socket, buffer, length, flags | MSG_DONTWAIT);
}
} // namespace

IdfExchange::IdfExchange(httpd_req_t *request, bool async) : request_(request), async_(async) {}
IdfExchange::~IdfExchange() {
    if (body_armed_)
        end_body(false);
    if (async_)
        httpd_req_async_handler_complete(request_);
}
HttpMethod IdfExchange::method() const {
    switch (request_->method) {
    case HTTP_GET:
        return HttpMethod::get;
    case HTTP_POST:
        return HttpMethod::post;
    case HTTP_PUT:
        return HttpMethod::put;
    case HTTP_PATCH:
        return HttpMethod::patch;
    case HTTP_DELETE:
        return HttpMethod::del;
    default:
        return HttpMethod::other;
    }
}
std::string IdfExchange::uri() const {
    return request_->uri;
}
std::string IdfExchange::header(const char *name, std::size_t limit) {
    const auto size = httpd_req_get_hdr_value_len(request_, name);
    if (!size || size > limit)
        return {};
    std::vector<char> text(size + 1);
    if (httpd_req_get_hdr_value_str(request_, name, text.data(), text.size()) != ESP_OK)
        return {};
    return text.data();
}
// Only the address counts: the source port changes with every connection.
uint32_t IdfExchange::client() {
    const int fd = httpd_req_to_sockfd(request_);
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (fd < 0 || getpeername(fd, reinterpret_cast<sockaddr *>(&address), &length) != 0)
        return 0;
    if (address.ss_family == AF_INET)
        return reinterpret_cast<sockaddr_in *>(&address)->sin_addr.s_addr;
    if (address.ss_family == AF_INET6)
        return client_identity_ipv6(reinterpret_cast<sockaddr_in6 *>(&address)->sin6_addr.s6_addr);
    return 0;
}
int IdfExchange::content_length() const {
    return static_cast<int>(request_->content_len);
}
bool IdfExchange::begin_body(int64_t deadline_us) {
    const int socket = httpd_req_to_sockfd(request_);
    if (socket < 0 ||
        httpd_sess_set_recv_override(request_->handle, socket, deadline_recv) != ESP_OK)
        return false;
    body_deadline_socket.store(socket);
    body_deadline_us.store(deadline_us);
    body_armed_ = true;
    return true;
}
void IdfExchange::end_body(bool close) {
    if (!body_armed_)
        return;
    body_armed_ = false;
    const int socket = httpd_req_to_sockfd(request_);
    body_deadline_us.store(0);
    body_deadline_socket.store(-1);
    // Give the connection its ordinary receive function back; the deadline override must not
    // outlive the request on a kept-alive socket.
    httpd_sess_set_recv_override(request_->handle, socket, plain_recv);
    if (close) {
        shutdown(socket, SHUT_RDWR);
        httpd_sess_trigger_close(request_->handle, socket);
    }
}
int IdfExchange::receive(char *buffer, std::size_t length) {
    return httpd_req_recv(request_, buffer, length);
}
int64_t IdfExchange::clock_us() {
    return esp_timer_get_time();
}
void IdfExchange::set_status(const char *status) {
    httpd_resp_set_status(request_, kept_.emplace_back(status).c_str());
}
void IdfExchange::set_type(const char *type) {
    httpd_resp_set_type(request_, kept_.emplace_back(type).c_str());
}
void IdfExchange::set_header(const char *name, std::string_view value) {
    const char *kept_name = kept_.emplace_back(name).c_str();
    httpd_resp_set_hdr(request_, kept_name, kept_.emplace_back(value).c_str());
}
esp_err_t IdfExchange::send(const char *data, std::size_t length) {
    return httpd_resp_send(request_, data, static_cast<ssize_t>(length));
}
esp_err_t IdfExchange::send_chunk(const char *data, std::size_t length) {
    return httpd_resp_send_chunk(request_, length ? data : nullptr, static_cast<ssize_t>(length));
}
void IdfExchange::close_connection() {
    const int socket = httpd_req_to_sockfd(request_);
    if (socket >= 0)
        httpd_sess_trigger_close(request_->handle, socket);
}
std::unique_ptr<HttpExchange> IdfExchange::begin_async() {
    httpd_req_t *copy = nullptr;
    if (httpd_req_async_handler_begin(request_, &copy) != ESP_OK || !copy)
        return nullptr;
    auto async = std::unique_ptr<HttpExchange>(new (std::nothrow) IdfExchange(copy, true));
    if (!async)
        httpd_req_async_handler_complete(copy);
    return async;
}
} // namespace monitor
