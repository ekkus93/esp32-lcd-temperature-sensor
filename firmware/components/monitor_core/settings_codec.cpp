#include "settings_codec.hpp"
#include <algorithm>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
namespace monitor {
namespace {
uint32_t checksum(const std::string &text) {
    uint32_t value = 2166136261u;
    for (unsigned char c : text) {
        value ^= c;
        value *= 16777619u;
    }
    return value;
}
} // namespace
std::string encode_settings(const std::vector<Sensor> &sensors) {
    if (sensors.size() > max_sensors)
        return {};
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "TMS1 " << sensors.size() << '\n' << std::setprecision(17);
    std::set<uint64_t> identities;
    for (const auto &sensor : sensors) {
        const auto &s = sensor.settings;
        if (!valid_rom(sensor.rom) || !valid_settings(s) || !identities.insert(sensor.rom).second)
            return {};
        out << sensor.rom << ' ' << std::quoted(s.name) << ' ' << std::quoted(s.short_name) << ' '
            << s.enabled << ' ' << s.disconnect_alert << ' ' << s.order << ' ' << s.calibration
            << ' ' << s.low.has_value() << ' ' << s.low.value_or(0) << ' ' << s.high.has_value()
            << ' ' << s.high.value_or(0) << ' ' << s.hysteresis << ' ' << s.persistence_ms << '\n';
    }
    std::string data = out.str();
    return data + std::to_string(checksum(data));
}
std::optional<std::vector<SavedSensor>> decode_settings(const std::string &bytes) {
    if (bytes.size() > 4096)
        return std::nullopt;
    auto offset = bytes.rfind('\n');
    if (offset == std::string::npos)
        return std::nullopt;
    std::istringstream tail(bytes.substr(offset + 1));
    uint64_t expected;
    if (!(tail >> expected) || expected != checksum(bytes.substr(0, offset + 1)))
        return std::nullopt;
    tail >> std::ws;
    if (!tail.eof())
        return std::nullopt;
    std::istringstream in(bytes.substr(0, offset + 1));
    in.imbue(std::locale::classic());
    std::string magic;
    std::size_t count;
    if (!(in >> magic >> count) || magic != "TMS1" || count > max_sensors)
        return std::nullopt;
    std::vector<SavedSensor> sensors;
    std::set<uint64_t> identities;
    for (std::size_t i = 0; i < count; ++i) {
        SavedSensor sensor{};
        auto &s = sensor.settings;
        bool low = false, high = false;
        double low_value = 0, high_value = 0;
        if (!(in >> sensor.rom >> std::quoted(s.name) >> std::quoted(s.short_name) >> s.enabled >>
              s.disconnect_alert >> s.order >> s.calibration >> low >> low_value >> high >>
              high_value >> s.hysteresis >> s.persistence_ms))
            return std::nullopt;
        if (low)
            s.low = low_value;
        if (high)
            s.high = high_value;
        if (!valid_rom(sensor.rom) || !valid_settings(s) || !identities.insert(sensor.rom).second)
            return std::nullopt;
        sensors.push_back(sensor);
    }
    in >> std::ws;
    if (!in.eof())
        return std::nullopt;
    return sensors;
}
} // namespace monitor
