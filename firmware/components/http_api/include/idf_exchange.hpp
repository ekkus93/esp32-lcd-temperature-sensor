#pragma once
#include "esp_http_server.h"
#include "http_exchange.hpp"
#include <deque>
namespace monitor {
// HttpExchange over ESP-IDF's HTTP server: the only request-handling code that uses httpd_req_t.
class IdfExchange final : public HttpExchange {
  public:
    // `async` marks an exchange made by begin_async(): it completes the request when destroyed.
    explicit IdfExchange(httpd_req_t *request, bool async = false);
    ~IdfExchange() override;
    IdfExchange(const IdfExchange &) = delete;
    IdfExchange &operator=(const IdfExchange &) = delete;

    HttpMethod method() const override;
    std::string uri() const override;
    std::string header(const char *name, std::size_t limit = 512) override;
    uint32_t client() override;
    int content_length() const override;
    bool begin_body(int64_t deadline_us) override;
    void end_body(bool close) override;
    int receive(char *buffer, std::size_t length) override;
    int64_t clock_us() override;
    void set_status(const char *status) override;
    void set_type(const char *type) override;
    void set_header(const char *name, std::string_view value) override;
    esp_err_t send(const char *data, std::size_t length) override;
    esp_err_t send_chunk(const char *data, std::size_t length) override;
    void close_connection() override;
    std::unique_ptr<HttpExchange> begin_async() override;
    void *native_handle() override {
        return request_;
    }

  private:
    httpd_req_t *request_;
    bool async_;
    bool body_armed_ = false;
    // httpd_resp_set_* keeps pointers until the answer is sent, so the strings live here.
    std::deque<std::string> kept_;
};
} // namespace monitor
