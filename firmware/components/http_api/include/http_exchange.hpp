#pragma once
#include "esp_err.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
namespace monitor {
enum class HttpMethod : uint8_t { get, post, put, patch, del, other };
// "GET", "POST", "PUT", "PATCH", "DELETE", or "" for anything else.
const char *http_method_name(HttpMethod method);

// One HTTP request and its answer (God file refactor, spec §4.2). The device implementation wraps
// ESP-IDF's httpd_req_t (IdfExchange); host tests use FakeExchange. Handlers use only this.
class HttpExchange {
  public:
    virtual ~HttpExchange() = default;

    // The request.
    virtual HttpMethod method() const = 0;
    virtual std::string uri() const = 0; // path and query, as received
    // A header's value, or "" when it is missing or longer than `limit` bytes.
    virtual std::string header(const char *name, std::size_t limit = 512) = 0;
    // A stable identity for the peer's address (not its port), 0 when unknown.
    virtual uint32_t client() = 0;
    virtual int content_length() const = 0;

    // Body receive primitives, used by read_json_body(). begin_body() arms a receive deadline for
    // this connection (false if it cannot); end_body() disarms it and, when `close`, closes the
    // connection. receive() follows httpd_req_recv(): bytes read, 0 at end, negative on error or
    // timeout.
    virtual bool begin_body(int64_t deadline_us) = 0;
    virtual void end_body(bool close) = 0;
    virtual int receive(char *buffer, std::size_t length) = 0;
    virtual int64_t clock_us() = 0;

    // The answer. Header names and values are copied, so temporaries are safe.
    virtual void set_status(const char *status) = 0;
    virtual void set_type(const char *type) = 0;
    virtual void set_header(const char *name, std::string_view value) = 0;
    virtual esp_err_t send(const char *data, std::size_t length) = 0;
    // One chunk of a chunked answer; a zero-length chunk ends it.
    virtual esp_err_t send_chunk(const char *data, std::size_t length) = 0;
    // Closes the connection after this request (the answer is then known to be incomplete or the
    // socket is not to be kept alive).
    virtual void close_connection() = 0;

    // Continues the answer on another task: the returned exchange answers the same request and
    // completes it when destroyed. nullptr when that is not possible.
    virtual std::unique_ptr<HttpExchange> begin_async() = 0;
    // The underlying httpd_req_t on the device, for code that must stream with ESP-IDF itself
    // (the OTA upload); nullptr elsewhere.
    virtual void *native_handle() = 0;
};
} // namespace monitor
