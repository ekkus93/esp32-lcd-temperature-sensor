#include "calibration_hold_request.hpp"
#include <cmath>
namespace monitor {
bool probe_can_be_held(State state) {
    return state == State::normal || state == State::high || state == State::low;
}
std::optional<int> calibration_hold_minutes(const cJSON *body) {
    if (!cJSON_IsObject(body) || cJSON_GetArraySize(body) != 1)
        return std::nullopt;
    const cJSON *minutes = cJSON_GetObjectItemCaseSensitive(body, "minutes");
    if (!cJSON_IsNumber(minutes) || !std::isfinite(minutes->valuedouble) ||
        minutes->valuedouble != std::floor(minutes->valuedouble) || minutes->valuedouble < 1 ||
        minutes->valuedouble > 20)
        return std::nullopt;
    return static_cast<int>(minutes->valuedouble);
}
} // namespace monitor
