#include "host_rules.hpp"
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
    HostRules rules;
    rules.station_ip = "192.168.1.50";
    rules.configured_name = "freezer-1";
    rules.active_name = "freezer-1";
    // The setup address, the station address and the configured name, with or without ":80".
    for (const char *host : {"192.168.4.1", "192.168.4.1:80", "192.168.1.50", "192.168.1.50:80",
                             "freezer-1.local", "freezer-1.local:80", "Freezer-1.LOCAL"})
        CHECK(host_allowed(host, rules));
    // Anything else is refused.
    for (const char *host :
         {"", ":80", "attacker.invalid", "freezer-1", "freezer-1.local.evil.com",
          "freezer-1.localx", "evil-freezer-1.local", "freezer-1.local:8080", "192.168.1.51",
          "192.168.4.2", "192.168.1.50:81", "localhost", "temperature-monitor.local"})
        CHECK(!host_allowed(host, rules));
    // After a rename the new name works at once and the old default no longer does.
    rules.configured_name = "garage";
    rules.active_name = "garage";
    CHECK(host_allowed("garage.local", rules));
    CHECK(!host_allowed("freezer-1.local", rules));
    // After an mDNS conflict the name in use (garage-2) is accepted along with the configured one.
    rules.active_name = "garage-2";
    CHECK(host_allowed("garage-2.local", rules) && host_allowed("garage.local:80", rules));
    // No station address yet (or the placeholder): only the setup address and names work.
    rules.station_ip = "";
    CHECK(!host_allowed("0.0.0.0", rules));
    rules.station_ip = "0.0.0.0";
    CHECK(!host_allowed("0.0.0.0", rules));
    CHECK(host_allowed("192.168.4.1", rules));
    std::cout << "PASS: Host allow-list follows the hostname and the active mDNS name\n";
}
