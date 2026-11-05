#include "sampling_service.hpp"
#include "fake_bus.hpp"

namespace monitor {
esp_err_t SamplingService::initialize(bool simulated) {
    simulated_ = simulated;
    return simulated_ ? ESP_OK : bus_.initialize();
}

std::vector<Reading> SamplingService::read(int64_t now_ms, const std::vector<uint64_t> &managed) {
    if (!simulated_)
        return bus_.read(now_ms, managed);
#ifdef CONFIG_MONITOR_SIM_REPLACEMENT_TEST
    return fake_replacement_readings();
#else
    return fake_readings();
#endif
}

bool SamplingService::fault() const {
    return !simulated_ && bus_.fault();
}

bool SamplingService::enabled() const {
    return !simulated_ && bus_.enabled();
}

bool SamplingService::monitored() const {
    return simulated_ || bus_.enabled() || bus_.init_failed();
}
} // namespace monitor
