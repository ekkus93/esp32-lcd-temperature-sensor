#include "wifi_scan_window.hpp"

#include <cassert>
#include <iostream>

int main() {
    monitor::WifiScanWindow scan;
    using Completion = monitor::WifiScanWindow::Completion;
    assert(!scan.active());
    assert(!scan.expired(100000));
    assert(scan.completed(100000) == Completion::ignored);

    // A driver failure or a candidate already using the radio must not
    // schedule an unrelated esp_wifi_scan_stop ten seconds later.
    scan.started(1000, false);
    assert(!scan.active());
    assert(!scan.expired(11000));
    assert(scan.completed(11000) == Completion::ignored);

    // An accepted scan has exactly one bounded ten-second deadline.
    scan.started(2000, true);
    assert(scan.active());
    assert(!scan.expired(11999));
    assert(scan.expired(12000));
    assert(!scan.expired(12001));
    assert(scan.completed(12001) == Completion::ignored);

    // Completion and candidate takeover both clear a previously armed timer.
    scan.started(30000, true);
    assert(scan.completed(30001) == Completion::accepted);
    assert(scan.completed(30002) == Completion::ignored);
    assert(!scan.expired(40000));
    scan.started(50000, true);
    scan.cancel();
    assert(scan.completed(50001) == Completion::ignored);
    assert(!scan.expired(60000));

    // A later rejected request must not inherit an earlier scan deadline.
    scan.started(70000, true);
    scan.started(71000, false);
    assert(!scan.active());
    assert(scan.completed(71001) == Completion::ignored);
    assert(!scan.expired(80000));

    // Repeated successful scans must replace the deadline, not accumulate it.
    scan.started(90000, true);
    scan.started(95000, true);
    assert(!scan.expired(100000));
    assert(scan.expired(105000));

    // A completion arriving at or after the deadline is stale even if the
    // worker has not yet processed the periodic timeout branch.
    scan.started(110000, true);
    assert(scan.completed(119999) == Completion::accepted);
    scan.started(120000, true);
    // Late completion is distinct from an already canceled scan. Firmware
    // must release scanning_ immediately instead of waiting for expired().
    assert(scan.completed(130000) == Completion::timed_out);
    assert(!scan.active());
    assert(scan.completed(130001) == Completion::ignored);
    assert(!scan.expired(130001));

    std::cout << "wifi scan deadline: stale completion isolation PASS\n";
}
