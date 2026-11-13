// Shared HTTP helpers over HttpExchange (God file refactor G01.1): errors, JSON answers, bounded
// JSON bodies and the session cookie.
#include "api_responses.hpp"
#include "fake_exchange.hpp"
#include <cstdlib>
#include <iostream>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

namespace {
FakeExchange posting(std::string body, const char *type = "application/json") {
    FakeExchange exchange;
    exchange.request_method = HttpMethod::post;
    exchange.body = std::move(body);
    exchange.request_headers["Content-Type"] = type;
    return exchange;
}
} // namespace

int main() {
    // Answers.
    {
        FakeExchange exchange;
        CHECK(send_error(exchange, "409 Conflict", "Probe must be connected") == ESP_OK);
        CHECK(exchange.status == "409 Conflict" && exchange.type == "application/json");
        CHECK(exchange.response_header("Cache-Control") == "no-store");
        CHECK(exchange.sent == R"({"error":"Probe must be connected"})");
        FakeExchange answer;
        Json object(cJSON_CreateObject(), cJSON_Delete);
        cJSON_AddNumberToObject(object.get(), "schema_version", 1);
        CHECK(send_json(answer, object.get()) == ESP_OK);
        CHECK(answer.status == "200 OK" && answer.sent == R"({"schema_version":1})");
        CHECK(answer.response_header("Cache-Control") == "no-store");
    }
    // A valid body.
    {
        auto exchange = posting(R"({"password":"x","minutes":5})");
        auto value = read_json_body(exchange);
        CHECK(value && json_string(value.get(), "password") == "x");
        CHECK(json_string(value.get(), "minutes").empty() &&
              json_string(value.get(), "no").empty());
        CHECK(!exchange.body_armed && !exchange.closed);
    }
    // No body, or no way to arm the deadline: nothing is read.
    {
        FakeExchange empty;
        CHECK(!read_json_body(empty) && !empty.body_armed);
        auto unarmed = posting("{}");
        unarmed.deadline_available = false;
        CHECK(!read_json_body(unarmed) && !unarmed.closed);
    }
    // Too large, or not JSON: drained (so the connection stays usable), refused, kept open.
    {
        auto large = posting("{\"a\":\"" + std::string(json_body_limit, 'x') + "\"}");
        CHECK(!read_json_body(large) && !large.closed && !large.body_armed);
        auto wrong = posting("{}", "text/plain");
        CHECK(!read_json_body(wrong) && !wrong.closed);
        auto missing = posting("{}", "");
        CHECK(!read_json_body(missing));
    }
    // Not an object, malformed, or with NUL bytes: refused.
    for (const char *text : {"[1]", "5", "\"s\"", "{", "{\"a\":1}x", "{\"a\":\"\\u0000\"}"}) {
        auto exchange = posting(text);
        CHECK(!read_json_body(exchange) && !exchange.closed);
    }
    {
        auto raw = posting(std::string("{\"a\":\"x\0y\"}", 12));
        CHECK(!read_json_body(raw));
    }
    // A client that stalls past the deadline, or whose connection fails, is disconnected.
    {
        auto stalled = posting(R"({"password":"secret"})");
        stalled.delivered_limit = 5;
        CHECK(!read_json_body(stalled) && stalled.closed && stalled.body_closed);
        auto failing = posting("{}");
        failing.receive_fails = true;
        CHECK(!read_json_body(failing) && failing.closed);
        auto large_stalled = posting(std::string(json_body_limit + 10, ' '));
        large_stalled.delivered_limit = 100;
        CHECK(!read_json_body(large_stalled) && large_stalled.closed);
    }
    // The session cookie among others, with and without spaces.
    {
        FakeExchange exchange;
        exchange.request_headers["Cookie"] = "theme=dark; tm_session=abc123;other=1";
        CHECK(session_cookie(exchange) == "abc123");
        exchange.request_headers["Cookie"] = "tm_session=only";
        CHECK(session_cookie(exchange) == "only");
        exchange.request_headers["Cookie"] = "xtm_session=no; a=b";
        CHECK(session_cookie(exchange).empty());
        exchange.request_headers.erase("Cookie");
        CHECK(session_cookie(exchange).empty());
    }
    // Query strings and plain text answers.
    {
        FakeExchange exchange;
        exchange.request_uri = "/api/v1/history?from=1&to=2";
        CHECK(url_query(exchange) == "from=1&to=2");
        exchange.request_uri = "/api/v1/alerts";
        CHECK(url_query(exchange).empty());
        exchange.request_uri = "/api/v1/alerts?";
        CHECK(url_query(exchange).empty());
        exchange.request_uri = "/a?b?c";
        CHECK(url_query(exchange) == "b?c");
        CHECK(send_text(exchange, R"({"status":"queued"})") == ESP_OK);
        CHECK(exchange.sent == R"({"status":"queued"})");
    }
    CHECK(std::string(http_method_name(HttpMethod::del)) == "DELETE");
    CHECK(std::string(http_method_name(HttpMethod::other)).empty());
    std::cout << "shared HTTP helpers: answers, bounded bodies and cookies\n";
}
