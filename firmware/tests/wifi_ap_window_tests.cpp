#include "wifi_ap_window.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    monitor::WifiApWindow ap;
    assert(!ap.should_close(1000000, false, true));

    // Explicit BOOT/setup recovery lasts ten minutes while online and must
    // not disappear when the router is offline at the expiry instant.
    ap.manual_open(1000);
    assert(!ap.should_close(600999, false, true));
    assert(!ap.should_close(601000, true, true));
    assert(!ap.should_close(601000, false, false));
    assert(ap.should_close(601000, false, true));
    assert(!ap.should_close(601001, false, true));

    // A successful candidate gets 30 seconds of setup/AP overlap.
    ap.candidate_started();
    ap.candidate_connected(2000000);
    assert(!ap.should_close(2029999, false, true));
    assert(ap.should_close(2030000, false, true));

    // If DHCP connectivity disappears before handoff, retain the AP even
    // beyond the old deadline. A later reconnect gets a fresh 30 seconds.
    ap.candidate_started();
    ap.candidate_connected(3000000);
    ap.station_disconnected();
    assert(!ap.should_close(3030000, false, false));
    assert(!ap.should_close(3100000, false, true));
    ap.station_reconnected(3100000);
    assert(!ap.should_close(3129999, false, true));
    assert(ap.should_close(3130000, false, true));

    // Starting a new candidate clears the old reconnect grace period.
    ap.candidate_connected(4000000);
    ap.station_disconnected();
    ap.candidate_started();
    ap.station_reconnected(4100000);
    assert(!ap.should_close(4300000, false, true));
    ap.candidate_connected(4400000);
    assert(!ap.should_close(4430000, true, true));
    assert(ap.should_close(4430000, false, true));

    // Manual BOOT recovery overrides a pending candidate timer and is not
    // shortened by later station reconnect events.
    ap.candidate_connected(5000000);
    ap.station_disconnected();
    ap.manual_open(5001000);
    ap.station_reconnected(5002000);
    assert(!ap.should_close(5030000, false, true));
    assert(ap.should_close(5601000, false, true));

    std::cout << "wifi AP deadline: handoff, disconnect, reconnect, BOOT hold PASS\n";
}
