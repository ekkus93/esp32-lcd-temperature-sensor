// Network routes over fakes (God file refactor G07).
#include "fake_exchange.hpp"
#include "fake_ports.hpp"
#include "routes_network.hpp"
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

namespace {
const std::optional<Session> owner = Session{"token", "csrf", 3600000};
FakeExchange call(esp_err_t (*route)(RouteRequest &, RoutePorts &), FakeDevice &device,
                  const std::string &body = "") {
    FakeExchange ex;
    ex.request_method = HttpMethod::post;
    ex.body = body;
    if (!body.empty())
        ex.request_headers["Content-Type"] = "application/json";
    const std::string path = "/api/v1/wifi";
    RouteRequest request{ex, path, owner, 0};
    route(request, device.ports);
    return ex;
}
} // namespace

int main() {
    FakeDevice device;
    auto status = call(route_wifi_status, device);
    CHECK(status.sent ==
          R"({"state":"connected","ip":"192.168.1.20","provisioning":false,)"
          R"("saved_credentials":true,"attempt":"","remaining_seconds":0,"error":""})");
    CHECK(status.sent.find("Home") == std::string::npos); // the SSID is not in this answer

    auto scan = call(route_wifi_scan_start, device);
    CHECK(scan.status == "202 Accepted" && scan.sent == "{}" &&
          device.network.log.count("scan") == 1);
    device.network.found = {{"Home", -40, 6, true}, {"Cafe", -80, 11, false}};
    device.network.scanning = true;
    auto results = call(route_wifi_scan_results, device);
    CHECK(results.sent ==
          R"({"scanning":true,"networks":[{"ssid":"Home","rssi":-40,"channel":6,)"
          R"("secure":true},{"ssid":"Cafe","rssi":-80,"channel":11,"secure":false}]})");

    auto connected = call(route_wifi_connect, device, R"({"ssid":"Home","password":"pw"})");
    CHECK(connected.status == "202 Accepted" && device.network.last_ssid == "Home");
    auto no_body = call(route_wifi_connect, device);
    CHECK(no_body.status == "400 Bad Request");
    CHECK(device.network.log.count("candidate_busy") == 1); // not asked without a body
    device.network.busy = true;
    auto busy = call(route_wifi_connect, device, R"({"ssid":"Home","password":"pw"})");
    CHECK(busy.status == "409 Conflict" && device.network.log.count("connect") == 1);
    device.network.busy = false;
    auto provision = call(route_wifi_provisioning, device);
    CHECK(provision.status == "202 Accepted" && device.network.log.count("provision") == 1);

    // The device refuses: scan too soon, results busy, connect and provisioning refused.
    device.network.accepted = false;
    CHECK(call(route_wifi_scan_start, device).status == "429 Too Many Requests");
    CHECK(call(route_wifi_scan_results, device).status == "503 Service Unavailable");
    CHECK(call(route_wifi_connect, device, R"({"ssid":"x"})").status == "400 Bad Request");
    CHECK(call(route_wifi_provisioning, device).sent == R"({"error":"Network control busy"})");
    std::cout << "network routes: status, scan, results, connect and provisioning\n";
}
