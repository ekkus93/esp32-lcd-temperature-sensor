#include "retirement_policy.hpp"
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
    constexpr uint64_t rom = 0x2801000000000029;
    ProbeScanStatus scan;
    CHECK(!can_retire_sensor(State::normal, rom, scan, true));
    CHECK(!can_retire_sensor(State::disconnected, rom, scan, false));
    scan.complete = true;
    scan.discovered_roms = {rom};
    CHECK(!can_retire_sensor(State::disconnected, rom, scan, false));
    CHECK(can_retire_sensor(State::disconnected, rom, scan, true));
    scan.discovered_roms.clear();
    CHECK(can_retire_sensor(State::disconnected, rom, scan, false));
    std::cout << "PASS: retirement requires disconnected state and a complete absent-ROM scan\n";
}
