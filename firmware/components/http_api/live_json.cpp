#include "live_json.hpp"
#include "api_responses.hpp"
#include <algorithm>
#include <cstdio>
#include <tuple>
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
std::string snapshot_json(RoutePorts &ports) {
    LiveView view;
    if (!ports.live.read(view))
        return {};
    const Registry &registry = view.registry;
    const int64_t uptime = view.uptime_ms;
    const bool logging = view.logging, synced = view.synchronized;
    const std::string &utc = view.utc;
    Json object(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddNumberToObject(object.get(), "version", 1);
    cJSON_AddNumberToObject(object.get(), "sequence", registry.sequence());
    cJSON_AddStringToObject(object.get(), "boot_id", ports.live.boot_id().c_str());
    cJSON_AddNumberToObject(object.get(), "uptime_ms", uptime);
    cJSON_AddBoolToObject(object.get(), "simulated", ports.live.simulated());
    cJSON_AddBoolToObject(object.get(), "bus_fault", registry.bus_fault());
    const auto update = ports.system.ota_status();
    auto *firmware = cJSON_AddObjectToObject(object.get(), "firmware");
    cJSON_AddStringToObject(firmware, "version", update.version.c_str());
    cJSON_AddStringToObject(firmware, "partition", update.partition.c_str());
    cJSON_AddStringToObject(firmware, "ota_state", update.state.c_str());
    auto *clock = cJSON_AddObjectToObject(object.get(), "clock");
    cJSON_AddBoolToObject(clock, "synchronized", synced);
    if (synced)
        cJSON_AddStringToObject(clock, "utc", utc.c_str());
    else
        cJSON_AddNullToObject(clock, "utc");
    auto *wifi = cJSON_AddObjectToObject(object.get(), "wifi");
    auto network = ports.network.info();
    cJSON_AddStringToObject(wifi, "state", network.state.c_str());
    // The name mDNS is really using (it changes after a conflict); null when mDNS is unavailable.
    if (const auto name = ports.network.mdns_hostname(); !name.empty())
        cJSON_AddStringToObject(wifi, "mdns_hostname", name.c_str());
    else
        cJSON_AddNullToObject(wifi, "mdns_hostname");
    auto *history = cJSON_AddObjectToObject(object.get(), "logging");
    cJSON_AddBoolToObject(history, "healthy", logging);
    cJSON_AddNumberToObject(history, "interval_seconds", 300);
    auto storage = ports.history.storage_info();
    cJSON_AddBoolToObject(history, "storage_available", storage.available);
    cJSON_AddNumberToObject(history, "storage_total_bytes", storage.total_bytes);
    cJSON_AddNumberToObject(history, "storage_used_bytes", storage.used_bytes);
    auto *sensors = cJSON_AddArrayToObject(object.get(), "sensors");
    auto ordered = registry.sensors();
    std::stable_sort(ordered.begin(), ordered.end(), [](const Sensor &a, const Sensor &b) {
        return std::tie(a.settings.order, a.rom) < std::tie(b.settings.order, b.rom);
    });
    for (const auto &sensor : ordered) {
        auto *s = cJSON_CreateObject();
        cJSON_AddItemToArray(sensors, s);
        cJSON_AddStringToObject(s, "rom_id", rom_hex(sensor.rom).c_str());
        cJSON_AddStringToObject(s, "name", sensor.settings.name.c_str());
        cJSON_AddStringToObject(s, "state", state_name(sensor.state));
        if (sensor.temperature)
            cJSON_AddNumberToObject(s, "temperature_c", *sensor.temperature);
        else
            cJSON_AddNullToObject(s, "temperature_c");
        if (sensor.last_seen_ms)
            cJSON_AddNumberToObject(s, "last_seen_ms", *sensor.last_seen_ms);
        else
            cJSON_AddNullToObject(s, "last_seen_ms");
        cJSON_AddBoolToObject(s, "enabled", sensor.settings.enabled);
        cJSON_AddNumberToObject(s, "order", sensor.settings.order);
        add_calibration_hold(s, sensor);
    }
    char *text = cJSON_PrintUnformatted(object.get());
    if (!text)
        return {};
    std::string result(text);
    cJSON_free(text);
    return result;
}
} // namespace monitor
