#pragma once
#include "cJSON.h"
#include "http_exchange.hpp"
#include <memory>
#include <string>
namespace monitor {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

// Request and response helpers shared by every route (God file refactor, spec §4.2).
// A JSON error body {"error": message} with the given status, `no-store`.
esp_err_t send_error(HttpExchange &exchange, const char *status, const char *message);
// A JSON answer, `no-store`.
esp_err_t send_json(HttpExchange &exchange, const cJSON *object);
// The request body as a JSON object, or null. At most 2,048 bytes of `application/json`, received
// within 4.5 seconds; an oversized or wrongly typed body is drained, and a body that times out or
// fails to arrive closes the connection. NUL bytes (raw or \u0000) are refused.
Json read_json_body(HttpExchange &exchange);
// A string member, or "" when missing or not a string.
std::string json_string(const cJSON *object, const char *key);
// The `tm_session` cookie's value, or "".
std::string session_cookie(HttpExchange &exchange);
constexpr std::size_t json_body_limit = 2048;
constexpr int64_t json_body_deadline_us = 4500000;
} // namespace monitor
