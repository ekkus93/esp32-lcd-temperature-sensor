#include "live_json.hpp"
#include <cstdio>
namespace monitor {
const char *event_kind_name(EventKind kind) {
    switch (kind) {
    case EventKind::sensor_state:
        return "sensor_state";
    case EventKind::wifi_state:
        return "wifi_state";
    case EventKind::clock_sync:
        return "clock_sync";
    case EventKind::reboot:
        return "reboot";
    case EventKind::storage_error:
        return "storage_error";
    case EventKind::calibration_started:
        return "calibration_started";
    case EventKind::calibration_ended:
        return "calibration_ended";
    }
    return "sensor_state";
}
// Whether a probe is in an ice-bath calibration hold (spec 17.3), and until when. A hold ends at
// its sample cycle once its time is up, so presence here is the current state.
void add_calibration_hold(cJSON *item, const Sensor &sensor) {
    cJSON_AddBoolToObject(item, "calibrating", sensor.calibration_hold_until_ms.has_value());
    if (sensor.calibration_hold_until_ms)
        cJSON_AddNumberToObject(item, "calibration_hold_ends_uptime_ms",
                                static_cast<double>(*sensor.calibration_hold_until_ms));
    else
        cJSON_AddNullToObject(item, "calibration_hold_ends_uptime_ms");
}

std::string boot_text(uint64_t boot_id) {
    char value[17]{};
    std::snprintf(value, sizeof(value), "%016llx", static_cast<unsigned long long>(boot_id));
    return value;
}
cJSON *sensor_json(const Sensor &sensor, const std::string &boot_id) {
    auto *item = cJSON_CreateObject();
    const auto &settings = sensor.settings;
    cJSON_AddStringToObject(item, "rom_id", rom_hex(sensor.rom).c_str());
    const std::string etag = "\"" + boot_id + ":" + rom_hex(sensor.rom) + ":" +
                             std::to_string(sensor.config_revision) + "\"";
    cJSON_AddStringToObject(item, "etag", etag.c_str());
    cJSON_AddNumberToObject(item, "revision", sensor.config_revision);
    cJSON_AddStringToObject(item, "name", settings.name.c_str());
    cJSON_AddStringToObject(item, "short_name", settings.short_name.c_str());
    cJSON_AddBoolToObject(item, "enabled", settings.enabled);
    cJSON_AddBoolToObject(item, "disconnect_alert", settings.disconnect_alert);
    cJSON_AddNumberToObject(item, "order", settings.order);
    cJSON_AddNumberToObject(item, "calibration_c", settings.calibration);
    cJSON_AddNumberToObject(item, "hysteresis_c", settings.hysteresis);
    cJSON_AddNumberToObject(item, "persistence_seconds", settings.persistence_ms / 1000.0);
    if (settings.low)
        cJSON_AddNumberToObject(item, "low_c", *settings.low);
    else
        cJSON_AddNullToObject(item, "low_c");
    if (settings.high)
        cJSON_AddNumberToObject(item, "high_c", *settings.high);
    else
        cJSON_AddNullToObject(item, "high_c");
    cJSON_AddStringToObject(item, "state", state_name(sensor.state));
    cJSON_AddStringToObject(item, "probe_calibration",
                            probe_calibration_name(sensor.probe_calibration));
    add_calibration_hold(item, sensor);
    return item;
}
} // namespace monitor
