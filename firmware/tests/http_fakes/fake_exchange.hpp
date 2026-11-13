#pragma once
// A recorded HTTP exchange for host tests (God file refactor, spec §4.2).
#include "http_exchange.hpp"
#include <map>
#include <string>
#include <vector>
namespace monitor {
class FakeExchange final : public HttpExchange {
  public:
    // The request.
    HttpMethod request_method = HttpMethod::get;
    std::string request_uri = "/";
    std::map<std::string, std::string> request_headers;
    uint32_t peer = 0x0100007f;
    std::string body;                       // what the client sends
    int declared_length = -1;               // Content-Length; -1 means body.size()
    std::size_t delivered_limit = SIZE_MAX; // bytes the client actually sends before stalling
    bool deadline_available = true;         // whether begin_body() can arm a deadline
    int64_t now_us = 1000000;
    // On the device a stalled receive blocks until the body deadline; by default it passes here
    // too.
    int64_t stall_step_us = 5000000;
    bool receive_fails = false;

    // The answer, as recorded.
    std::string status = "200 OK";
    std::string type;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string sent;
    std::vector<std::string> chunks;
    bool chunked_finished = false;
    int sends = 0;
    bool closed = false;
    bool body_armed = false, body_closed = false;
    int async_started = 0;
    esp_err_t send_result = ESP_OK;

    std::string response_header(const std::string &name) const {
        for (const auto &[key, value] : headers)
            if (key == name)
                return value;
        return {};
    }

    HttpMethod method() const override {
        return request_method;
    }
    std::string uri() const override {
        return request_uri;
    }
    std::string header(const char *name, std::size_t limit = 512) override {
        const auto found = request_headers.find(name);
        if (found == request_headers.end() || found->second.empty() || found->second.size() > limit)
            return {};
        return found->second;
    }
    uint32_t client() override {
        return peer;
    }
    int content_length() const override {
        return declared_length >= 0 ? declared_length : static_cast<int>(body.size());
    }
    bool begin_body(int64_t) override {
        body_armed = deadline_available;
        return deadline_available;
    }
    void end_body(bool close) override {
        body_armed = false;
        body_closed = close;
        if (close)
            closed = true;
    }
    int receive(char *buffer, std::size_t length) override {
        if (receive_fails)
            return -1;
        const auto available = std::min(body.size(), delivered_limit);
        if (read_ >= available) {
            now_us += stall_step_us;              // the client is silent: time passes
            return read_ >= body.size() ? 0 : -3; // HTTPD_SOCK_ERR_TIMEOUT-like
        }
        const auto count = std::min(length, available - read_);
        std::copy(body.begin() + static_cast<long>(read_),
                  body.begin() + static_cast<long>(read_ + count), buffer);
        read_ += count;
        return static_cast<int>(count);
    }
    int64_t clock_us() override {
        return now_us;
    }
    void set_status(const char *value) override {
        status = value;
    }
    void set_type(const char *value) override {
        type = value;
    }
    void set_header(const char *name, std::string_view value) override {
        headers.emplace_back(name, std::string(value));
    }
    esp_err_t send(const char *data, std::size_t length) override {
        ++sends;
        sent.assign(data, length);
        return send_result;
    }
    esp_err_t send_chunk(const char *data, std::size_t length) override {
        if (!length)
            chunked_finished = true;
        else
            chunks.emplace_back(data, length);
        return send_result;
    }
    void close_connection() override {
        closed = true;
    }
    std::unique_ptr<HttpExchange> begin_async() override {
        ++async_started;
        return nullptr;
    }
    void *native_handle() override {
        return nullptr;
    }

  private:
    std::size_t read_ = 0;
};
} // namespace monitor
