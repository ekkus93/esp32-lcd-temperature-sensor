#include "display_model.hpp"
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
    // Compact screen with an owner code: Wi-Fi page, owner-code page, then the readings.
    CHECK(setup_phase(0, true, true) == SetupPhase::wifi);
    CHECK(setup_phase(9999, true, true) == SetupPhase::wifi);
    CHECK(setup_phase(10000, true, true) == SetupPhase::owner_code);
    CHECK(setup_phase(19999, true, true) == SetupPhase::owner_code);
    CHECK(setup_phase(20000, true, true) == SetupPhase::readings);
    CHECK(setup_phase(29999, true, true) == SetupPhase::readings);
    CHECK(setup_phase(30000, true, true) == SetupPhase::wifi);

    // Compact screen with no owner code to show (already enrolled), or a large screen where one
    // page carries everything: setup, then the readings.
    for (bool compact : {true, false}) {
        const bool owner = !compact; // large layout: owner code present, still a single page
        CHECK(setup_phase(0, compact, owner && !compact) == SetupPhase::wifi);
        CHECK(setup_phase(10000, compact, owner && !compact) == SetupPhase::readings);
        CHECK(setup_phase(20000, compact, owner && !compact) == SetupPhase::wifi);
    }
    CHECK(setup_phase(10000, false, true) == SetupPhase::readings); // large screen never splits

    // Never a page the screen can't show, and the readings always get their turn.
    for (bool compact : {true, false})
        for (bool owner : {true, false}) {
            bool seen_readings = false;
            for (int64_t t = 0; t < 120000; t += 1000) {
                const auto phase = setup_phase(t, compact, owner);
                if (phase == SetupPhase::owner_code)
                    CHECK(compact && owner);
                seen_readings = seen_readings || phase == SetupPhase::readings;
            }
            CHECK(seen_readings);
        }
    // A clock that has not started (negative) falls back to the first page.
    CHECK(setup_phase(-5, true, true) == SetupPhase::wifi);
    std::cout << "PASS: setup mode alternates setup pages with the readings\n";
}
