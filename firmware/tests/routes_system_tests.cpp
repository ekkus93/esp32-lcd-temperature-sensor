// System routes over fakes (God file refactor G08): status, clock, probe scan, firmware upload,
// restart and factory reset, with each refusal changing nothing.
#include "fake_exchange.hpp"
#include "fake_ports.hpp"
#include "routes_system.hpp"
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
                  const std::string &body = "", int declared = -1) {
    FakeExchange ex;
    ex.request_method = HttpMethod::post;
    ex.body = body;
    ex.declared_length = declared;
    if (!body.empty())
        ex.request_headers["Content-Type"] = "application/json";
    const std::string path = "/api/v1/system";
    RouteRequest request{ex, path, owner, 0};
    route(request, device.ports);
    return ex;
}
} // namespace

int main() {
    FakeDevice device;
    // GET /status: the snapshot, or 503 when the published state is busy.
    auto status = call(route_status, device);
    CHECK(status.status == "200 OK" && status.type == "application/json");
    CHECK(status.response_header("Cache-Control") == "no-store");
    CHECK(status.sent.rfind(R"({"version":1,)", 0) == 0);
    device.live.busy = true;
    CHECK(call(route_status, device).sent == R"({"error":"Snapshot busy"})");
    CHECK(call(route_probe_scan, device).sent == R"({"error":"Probe scan status busy"})");
    device.live.busy = false;
    // GET /clock, before and after synchronization.
    CHECK(call(route_clock, device).sent == R"({"schema_version":1,"synchronized":false,)"
                                            R"("last_sync_utc_seconds":null,"sync_age_ms":null})");
    device.system.clock = {true, 1800000000, 65000};
    CHECK(call(route_clock, device).sent ==
          R"({"schema_version":1,"synchronized":true,)"
          R"("last_sync_utc_seconds":1800000000,"sync_age_ms":65000})");
    // GET /probe-scan.
    device.live.view.probe_scan.discovered_count = 3;
    device.live.view.probe_scan.complete = true;
    device.live.view.probe_scan.enabled = true;
    CHECK(
        call(route_probe_scan, device).sent ==
        R"({"schema_version":1,"enabled":true,"complete":true,"discovered_count":3,"overflow":false})");
    CHECK(device.side_effects() == 0);

    // POST /system/reboot: a body is refused (and the connection closed), unavailable is refused.
    auto with_body = call(route_reboot, device, "{}");
    CHECK(with_body.status == "400 Bad Request" && with_body.closed);
    device.system.can_reboot = false;
    CHECK(call(route_reboot, device).status == "503 Service Unavailable");
    CHECK(device.side_effects() == 0);
    device.system.can_reboot = true;
    auto reboot = call(route_reboot, device);
    CHECK(reboot.status == "202 Accepted" && reboot.sent == R"({"status":"rebooting"})");
    CHECK(device.system.log.count("schedule_reboot") == 1 &&
          device.system.log.count("restart_now") == 0);
    // When scheduling fails after the answer, it restarts at once.
    device.system.scheduled = false;
    call(route_reboot, device);
    CHECK(device.system.log.count("restart_now") == 1);
    device.system.scheduled = true;

    // POST /system/factory-reset: exactly {"erase_history": boolean}.
    const int before = device.side_effects();
    for (const char *invalid : {"", "{}", R"({"erase_history":1})", R"({"erase_history":"yes"})",
                                R"({"erase_history":true,"x":1})", "[true]"})
        CHECK(call(route_factory_reset, device, invalid).status == "400 Bad Request");
    CHECK(device.side_effects() == before);
    auto reset = call(route_factory_reset, device, R"({"erase_history":false})");
    CHECK(reset.status == "202 Accepted" && reset.sent == R"({"status":"resetting"})");
    CHECK(device.system.reset_erase_history == false);
    device.system.scheduled = false;
    CHECK(call(route_factory_reset, device, R"({"erase_history":true})").sent ==
          R"({"error":"Could not schedule factory reset"})");

    // POST /ota hands the request to the upload.
    auto ota = call(route_ota, device);
    CHECK(device.system.log.count("upload_firmware") == 1 && ota.status == "202 Accepted");
    std::cout << "system routes: status, clock, probe scan, OTA, reboot and factory reset\n";
}
