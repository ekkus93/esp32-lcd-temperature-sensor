#include "api_responses.hpp"
#include "bounded_body_reader.hpp"
#include <algorithm>
namespace monitor {
namespace {
int exchange_receive(void *context, char *buffer, std::size_t length) {
    return static_cast<HttpExchange *>(context)->receive(buffer, length);
}
int64_t exchange_clock(void *context) {
    return static_cast<HttpExchange *>(context)->clock_us();
}
// Disarms the body deadline on every exit, closing the connection when the body went wrong.
class BodyDeadline {
  public:
    explicit BodyDeadline(HttpExchange &exchange)
        : exchange_(exchange), deadline_us_(exchange.clock_us() + json_body_deadline_us),
          armed_(exchange.begin_body(deadline_us_)) {}
    ~BodyDeadline() {
        if (armed_)
            exchange_.end_body(close_);
    }
    bool armed() const {
        return armed_;
    }
    int64_t deadline_us() const {
        return deadline_us_;
    }
    void close_on(BodyReadStatus status) {
        if (status == BodyReadStatus::timeout || status == BodyReadStatus::receive_error)
            close_ = true;
    }

  private:
    HttpExchange &exchange_;
    int64_t deadline_us_;
    bool armed_;
    bool close_ = false;
};
} // namespace

const char *http_method_name(HttpMethod method) {
    switch (method) {
    case HttpMethod::get:
        return "GET";
    case HttpMethod::post:
        return "POST";
    case HttpMethod::put:
        return "PUT";
    case HttpMethod::patch:
        return "PATCH";
    case HttpMethod::del:
        return "DELETE";
    case HttpMethod::other:
        break;
    }
    return "";
}
esp_err_t send_error(HttpExchange &exchange, const char *status, const char *message) {
    exchange.set_status(status);
    exchange.set_type("application/json");
    exchange.set_header("Cache-Control", "no-store");
    Json object(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddStringToObject(object.get(), "error", message);
    char *text = cJSON_PrintUnformatted(object.get());
    if (!text)
        return ESP_ERR_NO_MEM;
    const auto result = exchange.send(text, std::char_traits<char>::length(text));
    cJSON_free(text);
    return result;
}
esp_err_t send_json(HttpExchange &exchange, const cJSON *object) {
    exchange.set_type("application/json");
    exchange.set_header("Cache-Control", "no-store");
    char *text = cJSON_PrintUnformatted(object);
    if (!text)
        return ESP_ERR_NO_MEM;
    const auto result = exchange.send(text, std::char_traits<char>::length(text));
    cJSON_free(text);
    return result;
}
Json read_json_body(HttpExchange &exchange) {
    const int length = exchange.content_length();
    if (length <= 0)
        return {nullptr, cJSON_Delete};
    BodyDeadline deadline(exchange);
    if (!deadline.armed())
        return {nullptr, cJSON_Delete};
    const bool oversized = static_cast<std::size_t>(length) > json_body_limit;
    const bool wrong_type = exchange.header("Content-Type", 128).find("application/json") != 0;
    if (oversized || wrong_type) {
        deadline.close_on(discard_exact_body(static_cast<std::size_t>(length),
                                             deadline.deadline_us(), &exchange, exchange_receive,
                                             exchange_clock));
        return {nullptr, cJSON_Delete};
    }
    std::string text;
    const auto received =
        read_exact_body(static_cast<std::size_t>(length), json_body_limit, deadline.deadline_us(),
                        &exchange, exchange_receive, exchange_clock, text);
    if (received != BodyReadStatus::complete) {
        deadline.close_on(received);
        return {nullptr, cJSON_Delete};
    }
    if (text.find('\0') != std::string::npos || text.find("\\u0000") != std::string::npos)
        return {nullptr, cJSON_Delete};
    Json value(cJSON_ParseWithLengthOpts(text.c_str(), text.size() + 1, nullptr, true),
               cJSON_Delete);
    std::fill(text.begin(), text.end(), 0);
    if (!value || !cJSON_IsObject(value.get()))
        return {nullptr, cJSON_Delete};
    return value;
}
std::string json_string(const cJSON *object, const char *key) {
    const auto *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : "";
}
std::string session_cookie(HttpExchange &exchange) {
    const auto text = exchange.header("Cookie");
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find(';', start);
        auto part = text.substr(start, end == std::string::npos ? end : end - start);
        while (!part.empty() && part.front() == ' ')
            part.erase(part.begin());
        if (part.rfind("tm_session=", 0) == 0)
            return part.substr(11);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return {};
}
} // namespace monitor
