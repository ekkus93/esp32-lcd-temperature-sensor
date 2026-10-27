#include "fake_bus.hpp"
#include "history_query.hpp"
#include "monitor_types.hpp"
#include <cstdlib>
#include <iostream>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
int main() {
    const auto rom = fake_readings(1)[0].rom;
    HistoryQuery q{123, 0, 86400000, 24, {rom}};
    CHECK(valid_query(q));
    bool has_max_points = false;
    const auto rom_query = parse_history_query(
        "from=0&to=86400000&max_points=24&rom_id=" + rom_hex(rom), &has_max_points);
    CHECK(rom_query && has_max_points && rom_query->roms.size() == 1 &&
          rom_query->roms.front() == rom);
    CHECK(!parse_history_query("from=0&to=86400000&rom_id=0000000000000000"));
    CHECK(!parse_history_query("from=0&to=86400000&rom_id=not-a-rom-id"));
    CHECK(!parse_history_query("from=0&from=1&to=86400000"));
    CHECK(!parse_history_query("from=0&to=86400000&max_points=513"));
    CHECK(!parse_history_query("from=0&to=86400000&unknown=1"));
    CHECK(!parse_history_query(std::string(257, 'x')));
    auto bounded_query = std::string("from=0&to=86400000");
    for (const auto &reading : fake_readings(max_sensors))
        bounded_query += "&rom_id=" + rom_hex(reading.rom);
    auto parsed_bounded_query = parse_history_query(bounded_query);
    CHECK(parsed_bounded_query && parsed_bounded_query->roms.size() == max_sensors);
    bounded_query += "&rom_id=" + rom_hex(rom);
    CHECK(!parse_history_query(bounded_query));
    const std::string parser_seed = "from=0&to=86400000&max_points=24&rom_id=" + rom_hex(rom);
    for (size_t position = 0; position < parser_seed.size(); ++position) {
        for (unsigned byte = 0; byte <= 0xff; ++byte) {
            auto mutated = parser_seed;
            mutated[position] = static_cast<char>(byte);
            if (const auto parsed_mutation = parse_history_query(mutated)) {
                CHECK(valid_query(*parsed_mutation));
                CHECK(parsed_mutation->roms.size() <= max_sensors);
            }
        }
    }
    HistoryReducer reduced(q);
    for (int64_t t = 0; t < q.to; t += bucket_ms)
        reduced.add({123, t, std::nullopt, {{rom, 20, -5, 40, 60, false}}});
    auto points = reduced.finish();
    CHECK(points.size() == 24 && points[0].count == 720);
    CHECK(points[0].average == 20 && points[0].minimum == -5 && points[0].maximum == 40);
    CHECK(!points[0].partial && !reduced.truncated());
    HistoryReducer gaps({123, 0, 3600000, 1, {}});
    gaps.add({123, 0, {}, {{rom, 10, 9, 11, 60, false}}});
    gaps.add({124, bucket_ms, {}, {{rom, 100, 100, 100, 60, false}}});
    gaps.add({123, 2 * bucket_ms, {}, {{rom, 20, 19, 21, 30, false}}});
    points = gaps.finish();
    CHECK(points.size() == 1 && points[0].count == 90 && points[0].partial);
    CHECK(points[0].average > 13.33 && points[0].average < 13.34);
    const auto explicit_gaps = history_gaps({
        {123, rom, 0, bucket_ms, {}, 10, 9, 11, 60, false},
        {123, rom, 2 * bucket_ms, 3 * bucket_ms, {}, 12, 11, 13, 60, false},
        {124, rom, 3 * bucket_ms, 4 * bucket_ms, {}, 12, 11, 13, 60, false},
    });
    CHECK(explicit_gaps.size() == 1 && explicit_gaps[0].boot_id == 123 &&
          explicit_gaps[0].start_ms == bucket_ms && explicit_gaps[0].end_ms == 2 * bucket_ms);
    HistoryReducer utc({{}, 1800000000000, 1800003600000, 12, {}});
    utc.add({123, 0, {}, {{rom, 50, 50, 50, 60, false}}});
    CHECK(utc.finish().empty());
    utc.add({123, 0, 1800000000, {{rom, 20, 20, 20, 60, false}}});
    CHECK(utc.finish().size() == 1);
    utc.add({123, 0, 1800000000, {{rom, 90, 90, 90, 60, false}}});
    CHECK(utc.truncated() && utc.finish()[0].count == 60);
    q.max_points = 513;
    CHECK(!valid_query(q) && HistoryReducer(q).finish().empty());
    q.max_points = 1;
    q.to = INT64_MAX;
    CHECK(!valid_query(q));
    std::cout << "PASS: bounded history reduction, weighted averages, extrema, gaps, boot/time "
                 "isolation and overlap detection\n";
}
