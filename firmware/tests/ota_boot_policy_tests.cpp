#include "ota_boot_policy.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace monitor;
    OtaBootHealth healthy{2, true, true, true, true, true};
    assert(ota_boot_health_ready(healthy));
    healthy.completed_cycles = 1;
    assert(!ota_boot_health_ready(healthy));
    healthy.completed_cycles = 0;
    assert(!ota_boot_health_ready(healthy));
    healthy.completed_cycles = 2;
    const OtaBootHealth missing[] = {
        {2, false, true, true, true, true}, {2, true, false, true, true, true},
        {2, true, true, false, true, true}, {2, true, true, true, false, true},
        {2, true, true, true, true, false},
    };
    for (const auto &broken : missing)
        assert(!ota_boot_health_ready(broken));
    healthy.completed_cycles = 100;
    assert(ota_boot_health_ready(healthy));

    // The decision over time (fake clock, milliseconds since boot): a healthy image is accepted
    // as soon as it is ready, and one that never becomes healthy is rolled back after the grace
    // period instead of staying pending until someone resets the board.
    const OtaBootHealth ready{2, true, true, true, true, true};
    const OtaBootHealth never_http{20, true, true, true, false, true};
    const OtaBootHealth never_auth{20, true, true, false, false, true};
    const OtaBootHealth never_wifi{20, true, false, false, false, true};
    assert(ota_boot_grace_ms == 120000);
    assert(ota_boot_decision(ready, 10000) == OtaBootAction::mark_valid);
    assert(ota_boot_decision(ready, 10 * ota_boot_grace_ms) == OtaBootAction::mark_valid);
    for (const auto &broken : {never_http, never_auth, never_wifi}) {
        assert(ota_boot_decision(broken, 0) == OtaBootAction::wait);
        assert(ota_boot_decision(broken, ota_boot_grace_ms - 1) == OtaBootAction::wait);
        assert(ota_boot_decision(broken, ota_boot_grace_ms) == OtaBootAction::roll_back);
        assert(ota_boot_decision(broken, 2 * ota_boot_grace_ms) == OtaBootAction::roll_back);
    }
    // An image still waiting for its second cycle inside the grace period just waits.
    assert(ota_boot_decision(OtaBootHealth{1, true, true, true, true, true}, 5000) ==
           OtaBootAction::wait);
    // A healthy-looking image that never completes its cycles also rolls back.
    assert(ota_boot_decision(OtaBootHealth{0, true, true, true, true, true}, ota_boot_grace_ms) ==
           OtaBootAction::roll_back);
    std::cout << "OTA startup health gating PASS\n";
}
