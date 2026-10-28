#include "wifi_ap_window.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    monitor::WifiApWindow ap;
    assert(!ap.should_close(1000000, false));

    // Explicit BOOT/setup recovery stays available for 10 minutes.
    ap.manual_open(1000);
    assert(!ap.should_close(600999, false));
    assert(!ap.should_close(601000, true));
    assert(ap.should_close(601000, false));
    assert(!ap.should_close(601001, false));

    // Successful credential handoff gives clients 30 seconds to reconnect.
    ap.candidate_started();
    ap.candidate_connected(2000000);
    assert(!ap.should_close(2029999, false));
    assert(ap.should_close(2030000, false));

    // A new attempt must cancel that earlier success's pending shutdown.
    ap.candidate_connected(3000000);
    ap.candidate_started();
    assert(!ap.should_close(3030000, false)); // Candidate failed; AP stays up.
    assert(!ap.should_close(3600000, false));

    // On retry, only the new successful candidate may schedule AP shutdown.
    ap.candidate_started();
    ap.candidate_connected(4000000);
    assert(!ap.should_close(4030000, true));
    assert(ap.should_close(4030000, false));

    // Manual recovery during a pending handoff replaces the shorter timeout.
    ap.candidate_connected(5000000);
    ap.manual_open(5001000);
    assert(!ap.should_close(5030000, false));
    assert(ap.should_close(5601000, false));

    std::cout << "wifi AP deadline: retry cancellation, handoff, BOOT hold PASS\n";
}
