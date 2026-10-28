#include "wifi_scan_window.hpp"

#include <cassert>
#include <iostream>

int main() {
    monitor::WifiScanWindow scan;
    assert(!scan.active());
    assert(!scan.expired(100000));
    assert(!scan.completed(100000));

    // A driver failure or a candidate already using the radio must not
    // schedule an unrelated esp_wifi_scan_stop ten seconds later.
    scan.started(1000, false);
    assert(!scan.active());
    assert(!scan.expired(11000));
    assert(!scan.completed(11000));

    // An accepted scan has exactly one bounded ten-second deadline.
    scan.started(2000, true);
    assert(scan.active());
    assert(!scan.expired(11999));
    assert(scan.expired(12000));
    assert(!scan.expired(12001));
    assert(!scan.completed(12001));

    // Completion and candidate takeover both clear a previously armed timer.
    scan.started(30000, true);
    assert(scan.completed(30001));
    assert(!scan.completed(30002));
    assert(!scan.expired(40000));
    scan.started(50000, true);
    scan.cancel();
    assert(!scan.completed(50001));
    assert(!scan.expired(60000));

    // A later rejected request must not inherit an earlier scan deadline.
    scan.started(70000, true);
    scan.started(71000, false);
    assert(!scan.active());
    assert(!scan.completed(71001));
    assert(!scan.expired(80000));

    // Repeated successful scans must replace the deadline, not accumulate it.
    scan.started(90000, true);
    scan.started(95000, true);
    assert(!scan.expired(100000));
    assert(scan.expired(105000));

    // A completion arriving at or after the deadline is stale even if the
    // worker has not yet processed the periodic timeout branch.
    scan.started(110000, true);
    assert(scan.completed(119999));
    scan.started(120000, true);
    assert(!scan.completed(130000));
    assert(!scan.active());
    assert(!scan.expired(130001));

    std::cout << "wifi scan deadline: stale completion isolation PASS\n";
}
