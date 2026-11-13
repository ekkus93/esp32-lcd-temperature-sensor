#pragma once
// JSON for the published state: sensors, journal events and the live snapshot (God file refactor
// G05, G11). Shared by the sensor, alert and status routes and the WebSocket.
#include "api_routes.hpp"
#include "cJSON.h"
#include "event_store.hpp"
#include "monitor.hpp"
#include <string>
namespace monitor {
const char *event_kind_name(EventKind kind);
// Whether a probe is in an ice-bath calibration hold (spec 17.3), and until when.
void add_calibration_hold(cJSON *item, const Sensor &sensor);
std::string boot_text(uint64_t boot_id);
// One entry of GET /sensors, with its ETag for this boot.
cJSON *sensor_json(const Sensor &sensor, const std::string &boot_id);
// The live snapshot (GET /status and every WebSocket frame); "" when the published state is busy
// or the text cannot be built.
std::string snapshot_json(RoutePorts &ports);
} // namespace monitor
