#include "fake_bus.hpp"
#include "history_query.hpp"
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
