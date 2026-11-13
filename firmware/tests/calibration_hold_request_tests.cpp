// PUT /sensors/{rom}/calibration-hold request rules (review audit H09): which probes can be held
// (409 otherwise) and which minute values are accepted (400 otherwise). These are the functions the
// HTTP handler calls.
#include "calibration_hold_request.hpp"
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
std::optional<int> minutes(const char *json) {
    cJSON *body = cJSON_Parse(json);
    const auto result = calibration_hold_minutes(body);
    cJSON_Delete(body);
    return result;
}
} // namespace

int main() {
    // Connected (reading) probes only; everything else is 409.
    for (const auto state : {State::normal, State::high, State::low})
        CHECK(probe_can_be_held(state));
    for (const auto state : {State::unassigned, State::disabled, State::disconnected, State::stale,
                             State::invalid, State::bus_fault})
        CHECK(!probe_can_be_held(state));
    // Accepted: whole minutes 1 to 20, including the boundaries and an integral float.
    CHECK(minutes(R"({"minutes":1})") == 1);
    CHECK(minutes(R"({"minutes":15})") == 15);
    CHECK(minutes(R"({"minutes":20})") == 20);
    CHECK(minutes(R"({"minutes":20.0})") == 20);
    // Refused: out of range, fractional, wrong types, missing, extra keys, not an object.
    for (const char *json :
         {R"({"minutes":0})", R"({"minutes":21})", R"({"minutes":-1})", R"({"minutes":1.5})",
          R"({"minutes":19.999})", R"({"minutes":1e9})", R"({"minutes":-1e9})",
          R"({"minutes":"5"})", R"({"minutes":true})", R"({"minutes":null})", R"({"minutes":[5]})",
          R"({})", R"({"minute":5})", R"({"minutes":5,"extra":1})", R"([5])", R"(5)", R"("5")"})
        CHECK(!minutes(json));
    CHECK(!calibration_hold_minutes(nullptr)); // an unreadable body
    std::cout << "calibration hold requests: 409 unless reading, 1 to 20 whole minutes only\n";
}
