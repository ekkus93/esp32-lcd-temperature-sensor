#include "history_csv.hpp"
#include "external_allocator.hpp"
#include <algorithm>
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
CsvExportResult export_history_csv(const HistoryVisitFunction &visit,
                                   const std::vector<uint64_t> &roms, bool simulated,
                                   CsvExportSink &sink, std::size_t chunk_bytes) {
    bool started = false, start_ok = true;
    const auto start = [&]() {
        if (!started) {
            started = true;
            start_ok = sink.begin();
        }
        return start_ok;
    };
    // Rows are sent in chunks of about 4 KiB. One chunk per row (about 110 bytes) meant three
    // small socket writes per row; a month of history took over six seconds, during which the
    // single HTTP task answered nothing else. On the device the buffer is in PSRAM.
    std::vector<char, ExternalAllocator<char>> pending;
    pending.reserve(chunk_bytes + 512);
    const auto flush = [&]() {
        if (pending.empty())
            return true;
        const bool sent = sink.send(pending.data(), pending.size());
        pending.clear();
        return sent;
    };
    HistoryVisitOptions options;
    options.stop_on_damage = true;
    CsvExportResult result;
    result.visit = visit(
        [&](const Bucket &bucket) {
            if (!start())
                return false;
            for (const auto &sensor : bucket.sensors) {
                if (!roms.empty() && std::find(roms.begin(), roms.end(), sensor.rom) == roms.end())
                    continue;
                const auto row = history_csv_row(bucket, sensor, simulated);
                pending.insert(pending.end(), row.begin(), row.end());
                if (pending.size() >= chunk_bytes && !flush())
                    return false;
            }
            return true;
        },
        options);
    if (!result.visit.complete()) {
        result.end = started ? CsvExportEnd::aborted : CsvExportEnd::refused;
        return result;
    }
    // A complete export with no matching record is the header row alone.
    if (!start() || !flush() || !sink.finish()) {
        // The client went away after the data: the file it holds lacks the final chunk.
        result.visit.end = HistoryVisitEnd::delivery_stopped;
        result.end = CsvExportEnd::aborted;
        return result;
    }
    result.end = CsvExportEnd::complete;
    return result;
}
} // namespace monitor
