// GET /history bodies stay schema-valid when allocations fail (review audit F7). Every allocation
// of the production builder (build_history_json, used by the HTTP handler) is failed in turn, once
// and from then on; each result is either "no answer" (the handler sends a constant 503) or a body
// that satisfies contracts/history.schema.json, and a body missing rows is marked truncated.
#include "cJSON.h"
#include "history_json.hpp"
#include "monitor_types.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

namespace {
// Allocation fault injection through cJSON's hooks.
long allocations = 0, fail_at = -1;
bool fail_after = false;
void *counting_malloc(size_t size) {
    ++allocations;
    if (allocations == fail_at || (fail_after && fail_at > 0 && allocations > fail_at))
        return nullptr;
    return std::malloc(size);
}
void use_counting_allocator(long at, bool after) {
    allocations = 0;
    fail_at = at;
    fail_after = after;
    cJSON_Hooks hooks{counting_malloc, std::free};
    cJSON_InitHooks(&hooks);
}
void use_plain_allocator() {
    cJSON_InitHooks(nullptr);
}

std::set<std::string> keys(const cJSON *object) {
    std::set<std::string> out;
    for (const cJSON *item = object->child; item; item = item->next)
        out.insert(item->string);
    return out;
}
std::set<std::string> schema_required(const cJSON *schema) {
    std::set<std::string> out;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(schema, "required"))
        out.insert(item->valuestring);
    return out;
}
const cJSON *schema_items(const cJSON *schema, const char *array) {
    return cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(schema, "properties"),
                                         array),
        "items");
}
bool integer(const cJSON *item, double minimum) {
    return cJSON_IsNumber(item) &&
           item->valuedouble == static_cast<double>(static_cast<int64_t>(item->valuedouble)) &&
           item->valuedouble >= minimum;
}
bool temperature(const cJSON *item) {
    return cJSON_IsNumber(item) && item->valuedouble >= -75 && item->valuedouble <= 145;
}
const std::regex boot_pattern("^[A-Za-z0-9_-]{1,32}$");
const std::regex rom_pattern("^28[0-9a-f]{14}$");
bool ids(const cJSON *row) {
    const auto *boot = cJSON_GetObjectItemCaseSensitive(row, "boot_id");
    const auto *rom = cJSON_GetObjectItemCaseSensitive(row, "rom_id");
    return cJSON_IsString(boot) && std::regex_match(boot->valuestring, boot_pattern) &&
           cJSON_IsString(rom) && std::regex_match(rom->valuestring, rom_pattern);
}

// contracts/history.schema.json, checked by hand: the key sets come from the schema file itself.
struct Schema {
    std::set<std::string> root, point, gap;
    Schema() {
        std::ifstream file(HISTORY_SCHEMA_PATH);
        std::stringstream text;
        text << file.rdbuf();
        cJSON *schema = cJSON_Parse(text.str().c_str());
        CHECK(schema);
        root = schema_required(schema);
        point = schema_required(schema_items(schema, "points"));
        gap = schema_required(schema_items(schema, "gaps"));
        cJSON_Delete(schema);
        CHECK(root.size() == 6 && point.size() == 10 && gap.size() == 4);
    }
    // Returns the parsed body's `truncated`, or fails the test if the body is not schema-valid.
    bool validate(const char *body, std::size_t *point_rows = nullptr,
                  std::size_t *gap_rows = nullptr) const {
        cJSON *json = cJSON_Parse(body);
        CHECK(json && cJSON_IsObject(json));
        CHECK(keys(json) == root); // additionalProperties: false, all required
        const auto *version = cJSON_GetObjectItemCaseSensitive(json, "schema_version");
        CHECK(cJSON_IsNumber(version) && version->valuedouble == 1);
        CHECK(cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(json, "simulated")));
        const auto *truncated = cJSON_GetObjectItemCaseSensitive(json, "truncated");
        CHECK(cJSON_IsBool(truncated));
        const auto *basis = cJSON_GetObjectItemCaseSensitive(json, "time_basis");
        CHECK(cJSON_IsString(basis) && (std::strcmp(basis->valuestring, "utc") == 0 ||
                                        std::strcmp(basis->valuestring, "uptime") == 0));
        const auto *points = cJSON_GetObjectItemCaseSensitive(json, "points");
        const auto *gaps = cJSON_GetObjectItemCaseSensitive(json, "gaps");
        CHECK(cJSON_IsArray(points) && cJSON_GetArraySize(points) <= 2560);
        CHECK(cJSON_IsArray(gaps) && cJSON_GetArraySize(gaps) <= 512);
        const cJSON *row = nullptr;
        cJSON_ArrayForEach(row, points) {
            CHECK(cJSON_IsObject(row) && keys(row) == point && ids(row));
            CHECK(integer(cJSON_GetObjectItemCaseSensitive(row, "start_ms"), 0));
            CHECK(integer(cJSON_GetObjectItemCaseSensitive(row, "end_ms"), 1));
            const auto *utc = cJSON_GetObjectItemCaseSensitive(row, "utc_seconds");
            CHECK(cJSON_IsNull(utc) || integer(utc, -1e18));
            CHECK(temperature(cJSON_GetObjectItemCaseSensitive(row, "average_c")));
            CHECK(temperature(cJSON_GetObjectItemCaseSensitive(row, "minimum_c")));
            CHECK(temperature(cJSON_GetObjectItemCaseSensitive(row, "maximum_c")));
            CHECK(integer(cJSON_GetObjectItemCaseSensitive(row, "count"), 1));
            CHECK(cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(row, "partial")));
        }
        cJSON_ArrayForEach(row, gaps) {
            CHECK(cJSON_IsObject(row) && keys(row) == gap && ids(row));
            CHECK(integer(cJSON_GetObjectItemCaseSensitive(row, "start_ms"), 0));
            CHECK(integer(cJSON_GetObjectItemCaseSensitive(row, "end_ms"), 1));
        }
        if (point_rows)
            *point_rows = static_cast<std::size_t>(cJSON_GetArraySize(points));
        if (gap_rows)
            *gap_rows = static_cast<std::size_t>(cJSON_GetArraySize(gaps));
        const bool result = cJSON_IsTrue(truncated);
        cJSON_Delete(json);
        return result;
    }
};

uint64_t probe_rom(unsigned serial) {
    uint8_t bytes[7] = {0x28, uint8_t(serial)};
    return 0x28 | uint64_t(serial) << 8 | uint64_t(crc8(bytes, 7)) << 56;
}
HistoryPoint point(unsigned serial, int64_t index, bool utc) {
    HistoryPoint p{0x1234abcd5678ef90ull,
                   probe_rom(serial),
                   index * 300000,
                   (index + 1) * 300000,
                   std::nullopt,
                   21.5,
                   20.0,
                   23.0,
                   60,
                   false};
    if (utc)
        p.utc_seconds = 1800000000 + index * 300;
    return p;
}

struct Case {
    const char *name;
    std::vector<HistoryPoint> points;
    std::vector<HistoryGap> gaps;
    bool truncated, simulated, uptime;
};
} // namespace

int main() {
    const Schema schema;
    std::vector<Case> cases;
    cases.push_back({"empty, zero gaps", {}, {}, false, false, false});
    cases.push_back({"empty, uptime basis, simulated", {}, {}, false, true, true});
    {
        Case c{"points without gaps", {}, {}, false, false, false};
        for (int i = 0; i < 6; ++i)
            c.points.push_back(point(1, i, true));
        cases.push_back(c);
    }
    {
        // A missing interval between points 2 and 5 makes a derived gap; one recorded gap too.
        Case c{"points and gaps", {}, {}, false, false, true};
        for (int i : {0, 1, 2, 5, 6})
            c.points.push_back(point(2, i, false));
        c.gaps.push_back({0x1234abcd5678ef90ull, probe_rom(3), 0, 600000});
        cases.push_back(c);
    }
    {
        Case c{"already truncated by the reducer", {}, {}, true, false, false};
        c.points.push_back(point(1, 0, true));
        cases.push_back(c);
    }

    for (const auto &c : cases) {
        // Unfaulted: complete, schema-valid, truncated only as the reducer said.
        use_counting_allocator(-1, false);
        char *body = build_history_json(c.points, c.gaps, c.truncated, c.simulated, c.uptime);
        const long total = allocations;
        use_plain_allocator();
        CHECK(body);
        std::size_t point_rows = 0, gap_rows = 0;
        CHECK(schema.validate(body, &point_rows, &gap_rows) == c.truncated);
        CHECK(point_rows == c.points.size());
        cJSON_free(body);
        // Every allocation failed in turn, once and from then on (envelope, rows, serialization).
        unsigned refused = 0, partial = 0;
        for (long at = 1; at <= total; ++at)
            for (bool after : {false, true}) {
                use_counting_allocator(at, after);
                char *faulted =
                    build_history_json(c.points, c.gaps, c.truncated, c.simulated, c.uptime);
                use_plain_allocator();
                if (!faulted) {
                    ++refused;
                    continue;
                }
                std::size_t rows = 0, gap_count = 0;
                const bool truncated = schema.validate(faulted, &rows, &gap_count);
                // A body that lost a row must say so; a body that says it is whole has every row.
                if (rows < c.points.size() || gap_count < gap_rows) {
                    CHECK(truncated);
                    ++partial;
                } else
                    CHECK(truncated == c.truncated);
                cJSON_free(faulted);
            }
        CHECK(refused > 0); // the envelope and serialization faults give no answer
        if (!c.points.empty())
            CHECK(partial > 0); // a row fault gives a valid, truncated subset
        std::cout << c.name << ": " << total << " allocations, " << refused << " faults refused, "
                  << partial << " valid partial answers\n";
    }

    // More than 512 gaps: the first 512 are sent and the answer is truncated.
    {
        std::vector<HistoryGap> many;
        for (int i = 0; i < 600; ++i)
            many.push_back({1, probe_rom(4), i * 600000LL, i * 600000LL + 300000});
        char *body = build_history_json({}, many, false, false, false);
        CHECK(body);
        std::size_t gap_rows = 0;
        CHECK(schema.validate(body, nullptr, &gap_rows) && gap_rows == 512);
        cJSON_free(body);
    }
    std::cout << "history JSON stays schema-valid or is refused under every allocation failure\n";
}
