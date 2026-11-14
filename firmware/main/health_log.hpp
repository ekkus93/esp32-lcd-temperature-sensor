#pragma once
// The per-cycle HEALTH and SENSOR_DIAG log lines (every cycle; every 20th in the simulation).
#include "history_service.hpp"
#include "monitor_loop.hpp"
#include "sampling_service.hpp"
namespace monitor {
void log_health(const MonitorLoop &loop, const SamplingService &sampling, HistoryService &history,
                bool simulated);
} // namespace monitor
