#include "clock_diagnostics.hpp"
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
    const auto unknown = make_clock_diagnostics({}, {}, 5000);
    CHECK(!unknown.synchronized && !unknown.last_sync_utc_seconds && !unknown.sync_age_ms);

    const auto synchronized = make_clock_diagnostics(1'798'000'000, 10'000, 75'000);
    CHECK(synchronized.synchronized && synchronized.last_sync_utc_seconds == 1'798'000'000);
    CHECK(synchronized.sync_age_ms == 65'000);

    // Wall-clock changes cannot affect age: it depends only on monotonic time.
    const auto after_wall_clock_step = make_clock_diagnostics(1'798'000'000, 10'000, 80'000);
    CHECK(after_wall_clock_step.sync_age_ms == 70'000);

    // A successful resync replaces the epoch and resets the monotonic age.
    const auto resynchronized = make_clock_diagnostics(1'798'000'100, 80'000, 81'500);
    CHECK(resynchronized.last_sync_utc_seconds == 1'798'000'100);
    CHECK(resynchronized.sync_age_ms == 1'500);

    // A monotonic input preceding the sync cannot underflow the reported age.
    const auto reordered = make_clock_diagnostics(1'798'000'100, 80'000, 79'000);
    CHECK(reordered.synchronized && reordered.sync_age_ms == 0);

    std::cout << "PASS: clock sync diagnostics use monotonic age and honest unknown state\n";
}
