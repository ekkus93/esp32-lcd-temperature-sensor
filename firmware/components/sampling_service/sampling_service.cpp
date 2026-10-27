#include "sampling_service.hpp"
#include "fake_bus.hpp"

namespace monitor {
esp_err_t SamplingService::initialize(bool simulated) {
    simulated_ = simulated;
    return simulated_ ? ESP_OK : bus_.initialize();
}

std::vector<Reading> SamplingService::read(int64_t now_ms) {
    return simulated_ ? fake_readings() : bus_.read(now_ms);
}

bool SamplingService::fault() const {
    return !simulated_ && bus_.fault();
}

bool SamplingService::enabled() const {
    return !simulated_ && bus_.enabled();
}
} // namespace monitor
