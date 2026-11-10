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
    // The setup window alternates the Wi-Fi page and the readings, ten seconds each.
    CHECK(setup_phase(0) == SetupPhase::wifi);
    CHECK(setup_phase(9999) == SetupPhase::wifi);
    CHECK(setup_phase(10000) == SetupPhase::readings);
    CHECK(setup_phase(19999) == SetupPhase::readings);
    CHECK(setup_phase(20000) == SetupPhase::wifi);
    CHECK(setup_phase(-5) == SetupPhase::wifi); // before boot: the Wi-Fi page
    std::cout << "PASS: setup window alternates the Wi-Fi page and the readings\n";
    return 0;
}
