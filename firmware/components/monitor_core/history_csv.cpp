#include "history_csv.hpp"
#include <iomanip>
#include <locale>
#include <sstream>
namespace monitor {
const char *history_csv_header() {
    return "schema_version,simulated,boot_id,timestamp_quality,start_uptime_ms,utc_seconds,rom_id,"
           "sensor_"
           "name,name_source,name_escaped,average_c,minimum_c,maximum_c,valid_count,partial,state_"
           "flags,units\r\n";
}
std::string history_csv_row(const Bucket &bucket, const Aggregate &sensor, bool simulated) {
    std::string name = sensor.name;
    const auto first = name.find_first_not_of(' ');
    const bool escaped =
        first != std::string::npos && std::string("=+-@").find(name[first]) != std::string::npos;
    if (escaped)
        name.insert(name.begin(), '\'');
    std::string quoted = "\"";
    for (char c : name) {
        quoted += c;
        if (c == '"')
            quoted += '"';
    }
    quoted += '"';
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "1," << simulated << ',' << std::hex << std::setfill('0') << std::setw(16)
        << bucket.boot_id << std::dec << ',' << (bucket.utc_seconds ? "utc" : "boot_uptime") << ','
        << bucket.start_uptime_ms << ',';
    if (bucket.utc_seconds)
        out << *bucket.utc_seconds;
    out << ',' << rom_hex(sensor.rom) << ',' << quoted << ','
        << (sensor.name.empty() ? "unknown" : "recorded") << ',' << escaped;
    out << std::fixed << std::setprecision(3) << ',';
    if (sensor.count)
        out << sensor.average;
    out << ',';
    if (sensor.count)
        out << sensor.minimum;
    out << ',';
    if (sensor.count)
        out << sensor.maximum;
    out << ',' << sensor.count << ',' << sensor.partial << ',' << std::hex << std::setw(4)
        << sensor.state_flags << ",C\r\n";
    return out.str();
}
} // namespace monitor
