#include "history_json.hpp"
#include "cJSON.h"
#include "monitor_types.hpp"
#include <algorithm>
#include <cstdio>
#include <memory>
namespace monitor {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
// Builds one complete row, or nothing.
Json point_row(const HistoryPoint &point) {
    Json item(cJSON_CreateObject(), cJSON_Delete);
    char boot[17];
    std::snprintf(boot, sizeof(boot), "%016llx", static_cast<unsigned long long>(point.boot_id));
    const bool built =
        item && cJSON_AddStringToObject(item.get(), "boot_id", boot) &&
        cJSON_AddStringToObject(item.get(), "rom_id", rom_hex(point.rom).c_str()) &&
        cJSON_AddNumberToObject(item.get(), "start_ms", point.start_ms) &&
        cJSON_AddNumberToObject(item.get(), "end_ms", point.end_ms) &&
        (point.utc_seconds ? cJSON_AddNumberToObject(item.get(), "utc_seconds", *point.utc_seconds)
                           : cJSON_AddNullToObject(item.get(), "utc_seconds")) &&
        cJSON_AddNumberToObject(item.get(), "average_c", point.average) &&
        cJSON_AddNumberToObject(item.get(), "minimum_c", point.minimum) &&
        cJSON_AddNumberToObject(item.get(), "maximum_c", point.maximum) &&
        cJSON_AddNumberToObject(item.get(), "count", point.count) &&
        cJSON_AddBoolToObject(item.get(), "partial", point.partial);
    return built ? std::move(item) : Json(nullptr, cJSON_Delete);
}
Json gap_row(const HistoryGap &gap) {
    Json item(cJSON_CreateObject(), cJSON_Delete);
    char boot[17];
    std::snprintf(boot, sizeof(boot), "%016llx", static_cast<unsigned long long>(gap.boot_id));
    const bool built = item && cJSON_AddStringToObject(item.get(), "boot_id", boot) &&
                       cJSON_AddStringToObject(item.get(), "rom_id", rom_hex(gap.rom).c_str()) &&
                       cJSON_AddNumberToObject(item.get(), "start_ms", gap.start_ms) &&
                       cJSON_AddNumberToObject(item.get(), "end_ms", gap.end_ms);
    return built ? std::move(item) : Json(nullptr, cJSON_Delete);
}
// Moves a complete row into the array; false (and the row freed) if it could not be attached.
bool attach(cJSON *array, Json row) {
    if (!row || !cJSON_AddItemToArray(array, row.get()))
        return false;
    row.release();
    return true;
}
} // namespace

char *build_history_json(const std::vector<HistoryPoint> &points,
                         std::vector<HistoryGap> gap_records, bool truncated, bool simulated,
                         bool uptime_basis) {
    Json output(cJSON_CreateObject(), cJSON_Delete);
    if (!output)
        return nullptr;
    cJSON *truncated_item = nullptr;
    cJSON *items = nullptr, *gaps = nullptr;
    // The envelope: every required field, or no answer.
    if (!cJSON_AddNumberToObject(output.get(), "schema_version", 1) ||
        !cJSON_AddBoolToObject(output.get(), "simulated", simulated) ||
        !(truncated_item = cJSON_AddBoolToObject(output.get(), "truncated", truncated)) ||
        !cJSON_AddStringToObject(output.get(), "time_basis", uptime_basis ? "uptime" : "utc") ||
        !(items = cJSON_AddArrayToObject(output.get(), "points")) ||
        !(gaps = cJSON_AddArrayToObject(output.get(), "gaps")))
        return nullptr;
    // Rows: each complete or left out. A row left out makes the answer truncated.
    bool rows_missing = false;
    for (const auto &point : points)
        if (!attach(items, point_row(point))) {
            rows_missing = true;
            break;
        }
    for (const auto &gap : history_gaps(points))
        if (std::find_if(gap_records.begin(), gap_records.end(), [&](const auto &item) {
                return item.boot_id == gap.boot_id && item.rom == gap.rom &&
                       item.start_ms == gap.start_ms && item.end_ms == gap.end_ms;
            }) == gap_records.end())
            gap_records.push_back(gap);
    if (gap_records.size() > 512) {
        gap_records.resize(512);
        rows_missing = true;
    }
    for (const auto &gap : gap_records)
        if (!attach(gaps, gap_row(gap))) {
            rows_missing = true;
            break;
        }
    // Setting a boolean allocates nothing, so a partial answer is always marked as such.
    if (rows_missing)
        cJSON_SetBoolValue(truncated_item, true);
    return cJSON_PrintUnformatted(output.get());
}
} // namespace monitor
