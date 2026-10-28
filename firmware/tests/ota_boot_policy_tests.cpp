#include "ota_boot_policy.hpp"
#include <cassert>
#include <iostream>

int main() {
    using monitor::OtaBootHealth;
    using monitor::ota_boot_health_ready;
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
    std::cout << "OTA startup health gating PASS\n";
}
