// Sensor routes over fakes (God file refactor G05): list, storage, alerts, edits, retirement and
// the calibration hold, with each refusal reaching no queue.
#include "fake_exchange.hpp"
#include "fake_ports.hpp"
#include "routes_sensors.hpp"
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
uint64_t probe(unsigned serial) {
    uint8_t bytes[7] = {0x28, uint8_t(serial)};
    return 0x28 | uint64_t(serial) << 8 | uint64_t(crc8(bytes, 7)) << 56;
}
// Two probes: the first is reading normally, the second is above its 30 °C limit.
Registry registry_with_alarm() {
    Registry registry;
    CHECK(registry.discover(probe(1)) && registry.discover(probe(2)));
    auto settings = registry.sensors()[1].settings;
    settings.name = "Exhaust";
    settings.high = 30;
    settings.persistence_ms = 0;
    CHECK(registry.configure(probe(2), settings));
    for (int64_t now = 0; now < 60000 && registry.sensors()[1].state != State::high; now += 5000)
        registry.sample({{probe(1), 21.0}, {probe(2), 35.0}}, now);
    CHECK(registry.sensors()[0].state == State::normal);
    CHECK(registry.sensors()[1].state == State::high);
    return registry;
}
FakeExchange request_for(HttpMethod method, const std::string &uri) {
    FakeExchange ex;
    ex.request_method = method;
    ex.request_uri = uri;
    return ex;
}
esp_err_t run(esp_err_t (*route)(RouteRequest &, RoutePorts &), FakeExchange &ex,
              FakeDevice &device, int64_t now_ms = 100000) {
    const std::string path = ex.request_uri.substr(0, ex.request_uri.find('?'));
    RouteRequest request{ex, path, owner, now_ms};
    return route(request, device.ports);
}
std::string etag(const FakeDevice &device, const Sensor &sensor) {
    return "\"" + device.live.boot + ":" + rom_hex(sensor.rom) + ":" +
           std::to_string(sensor.config_revision) + "\"";
}
} // namespace

int main() {
    const auto rom1 = rom_hex(probe(1)), rom2 = rom_hex(probe(2));
    // A busy published state answers 503 on every sensor route, before anything else.
    {
        FakeDevice device;
        device.live.busy = true;
        for (auto route : {route_alerts, route_sensor_list, route_sensor_storage,
                           route_calibration_hold, route_sensor_change}) {
            auto ex = request_for(HttpMethod::get, "/api/v1/sensors/" + rom1);
            run(route, ex, device);
            CHECK(ex.status == "503 Service Unavailable");
            CHECK(ex.sent == R"({"error":"Snapshot busy"})");
        }
        CHECK(device.side_effects() == 0);
    }
    // GET /sensors and /sensors/storage.
    {
        FakeDevice device;
        auto empty = request_for(HttpMethod::get, "/api/v1/sensors");
        run(route_sensor_list, empty, device);
        CHECK(empty.status == "200 OK" && empty.sent == "[]");
        device.live.view.registry = registry_with_alarm();
        auto list = request_for(HttpMethod::get, "/api/v1/sensors");
        run(route_sensor_list, list, device);
        const auto &exhaust = device.live.view.registry.sensors()[1];
        // The ETag is "boot:rom:revision", escaped inside the JSON string.
        const std::string tag = etag(device, exhaust);
        CHECK(list.sent.find("\"etag\":\"\\\"" + tag.substr(1, tag.size() - 2) + "\\\"\"") !=
              std::string::npos);
        CHECK(list.sent.find("\"name\":\"Exhaust\"") != std::string::npos);
        CHECK(list.sent.find("\"high_c\":30") != std::string::npos);
        CHECK(list.sent.find("\"state\":\"high\"") != std::string::npos);
        CHECK(list.sent.find("\"calibrating\":false") != std::string::npos);
        auto storage = request_for(HttpMethod::get, "/api/v1/sensors/storage");
        run(route_sensor_storage, storage, device);
        CHECK(storage.sent == R"({"schema_version":1,"state":"ready","writable":true})");
        device.updates.writable = false;
        auto read_only = request_for(HttpMethod::get, "/api/v1/sensors/storage");
        run(route_sensor_storage, read_only, device);
        CHECK(read_only.sent == R"({"schema_version":1,"state":"read_only","writable":false})");
    }
    // GET /alerts: the cursor, the active conditions and the journal page.
    {
        FakeDevice device;
        device.live.view.registry = registry_with_alarm();
        auto plain = request_for(HttpMethod::get, "/api/v1/alerts");
        run(route_alerts, plain, device);
        CHECK(plain.status == "200 OK");
        CHECK(device.history.last_after == 0 && device.history.last_limit == 50);
        CHECK(plain.sent.find("\"state\":\"high\"") != std::string::npos);
        CHECK(plain.sent.find("\"name\":\"Exhaust\"") != std::string::npos);
        CHECK(plain.sent.find("\"since_utc_seconds\":null") != std::string::npos);
        CHECK(plain.sent.find("wifi_disconnected") == std::string::npos);
        CHECK(plain.sent.find(rom1) == std::string::npos); // the normal probe is not listed
        auto cursor = request_for(HttpMethod::get, "/api/v1/alerts?after=7&limit=10");
        device.network.network.state = "offline";
        device.network.network.state_since_ms = 40000;
        device.system.utc = 1800000000;
        device.history.page.events.push_back({9, 0xabc, 1000, 1700000000, probe(2),
                                              EventKind::calibration_started, State::normal,
                                              State::normal, 0});
        device.history.page.next_after = 9;
        run(route_alerts, cursor, device);
        CHECK(device.history.last_after == 7 && device.history.last_limit == 10);
        CHECK(cursor.sent.find("\"state\":\"wifi_disconnected\",\"since_uptime_ms\":40000,"
                               "\"since_utc_seconds\":1799999940") != std::string::npos);
        CHECK(cursor.sent.find("\"kind\":\"calibration_started\"") != std::string::npos);
        CHECK(cursor.sent.find("\"boot_id\":\"0000000000000abc\"") != std::string::npos);
        CHECK(cursor.sent.find("\"next_after\":9") != std::string::npos);
        for (const char *bad : {"limit=0", "limit=51", "after=1&after=2", "page=1",
                                "after=", "after=x", "after=-1", "after=1&limit"}) {
            auto ex = request_for(HttpMethod::get, std::string("/api/v1/alerts?") + bad);
            run(route_alerts, ex, device);
            CHECK(ex.status == "400 Bad Request");
        }
        auto long_query =
            request_for(HttpMethod::get, "/api/v1/alerts?after=" + std::string(60, '1'));
        run(route_alerts, long_query, device);
        CHECK(long_query.status == "400 Bad Request");
    }
    // PUT/DELETE /sensors/{rom}/calibration-hold.
    {
        FakeDevice device;
        device.live.view.registry = registry_with_alarm();
        auto unknown =
            request_for(HttpMethod::put, "/api/v1/sensors/2800000000000000/calibration-hold");
        run(route_calibration_hold, unknown, device);
        CHECK(unknown.status == "404 Not Found");
        auto start = request_for(HttpMethod::put, "/api/v1/sensors/" + rom1 + "/calibration-hold");
        start.body = R"({"minutes":15})";
        start.request_headers["Content-Type"] = "application/json";
        run(route_calibration_hold, start, device);
        CHECK(start.status == "202 Accepted" && start.sent == R"({"status":"queued"})");
        CHECK(device.updates.holds.size() == 1 && device.updates.holds[0].duration_ms == 900000);
        CHECK(!device.updates.holds[0].cancel);
        auto bad = request_for(HttpMethod::put, "/api/v1/sensors/" + rom1 + "/calibration-hold");
        bad.body = R"({"minutes":21})";
        bad.request_headers["Content-Type"] = "application/json";
        run(route_calibration_hold, bad, device);
        CHECK(bad.status == "400 Bad Request");
        auto end = request_for(HttpMethod::del, "/api/v1/sensors/" + rom1 + "/calibration-hold");
        run(route_calibration_hold, end, device);
        CHECK(end.status == "202 Accepted" && device.updates.holds.back().cancel);
        auto end_body =
            request_for(HttpMethod::del, "/api/v1/sensors/" + rom1 + "/calibration-hold");
        end_body.body = "{}";
        run(route_calibration_hold, end_body, device);
        CHECK(end_body.status == "400 Bad Request");
        device.updates.result = QueueResult::full;
        auto full = request_for(HttpMethod::del, "/api/v1/sensors/" + rom1 + "/calibration-hold");
        run(route_calibration_hold, full, device);
        CHECK(full.sent == R"({"error":"Calibration request queue full"})");
        CHECK(device.updates.holds.size() == 2);
    }
    // PATCH/DELETE /sensors/{rom}: every refusal before the queue, then the queued edit.
    {
        FakeDevice device;
        device.live.view.registry = registry_with_alarm();
        const auto &first = device.live.view.registry.sensors()[0];
        auto patch = [&](const std::string &body, const std::string &match) {
            auto ex = request_for(HttpMethod::patch, "/api/v1/sensors/" + rom1);
            ex.body = body;
            ex.request_headers["Content-Type"] = "application/json";
            if (!match.empty())
                ex.request_headers["If-Match"] = match;
            run(route_sensor_change, ex, device);
            return ex;
        };
        device.updates.writable = false;
        CHECK(patch(R"({"name":"A"})", etag(device, first)).status == "503 Service Unavailable");
        device.updates.writable = true;
        device.updates.pending = true;
        CHECK(patch(R"({"name":"A"})", etag(device, first)).status == "409 Conflict");
        device.updates.pending = false;
        CHECK(patch(R"({"name":"A"})", "").status == "428 Precondition Required");
        CHECK(patch(R"({"name":"A"})", "\"stale\"").status == "412 Precondition Failed");
        CHECK(patch("", etag(device, first)).status == "400 Bad Request");
        for (const char *invalid :
             {"{}", R"({"name":5})", R"({"enabled":"yes"})", R"({"low_c":"x"})", R"({"order":5})",
              R"({"order":1.5})", R"({"persistence_seconds":3601})", R"({"color":"red"})",
              R"({"name":"a","name":"b"})", R"({"probe_calibration":"stored"})"})
            CHECK(patch(invalid, etag(device, first)).status == "400 Bad Request");
        CHECK(device.updates.side_effects() == 0);
        auto ok = patch(R"({"name":"Canopy","high_c":28.5,"low_c":null,"order":1,)"
                        R"("persistence_seconds":12.3456,"enabled":false})",
                        etag(device, first));
        CHECK(ok.status == "202 Accepted" && device.updates.updates.size() == 1);
        const auto &queued = device.updates.updates[0];
        CHECK(queued.rom == probe(1) && !queued.retire && queued.settings.name == "Canopy");
        CHECK(queued.settings.high == 28.5 && !queued.settings.low && queued.settings.order == 1);
        CHECK(queued.settings.persistence_ms == 12346 && !queued.settings.enabled);
        // Retirement: refused while the probe still reads, refused with a body.
        auto retire = request_for(HttpMethod::del, "/api/v1/sensors/" + rom1);
        retire.request_headers["If-Match"] = etag(device, first);
        run(route_sensor_change, retire, device);
        CHECK(retire.status == "409 Conflict");
        CHECK(retire.sent.find("absent from a complete scan") != std::string::npos);
        auto retire_body = request_for(HttpMethod::del, "/api/v1/sensors/" + rom1);
        retire_body.request_headers["If-Match"] = etag(device, first);
        retire_body.body = "{}";
        run(route_sensor_change, retire_body, device);
        CHECK(retire_body.status == "400 Bad Request");
        CHECK(device.updates.updates.size() == 1);
        auto gone = request_for(HttpMethod::patch, "/api/v1/sensors/2800000000000000");
        run(route_sensor_change, gone, device);
        CHECK(gone.status == "404 Not Found");
        (void)rom2;
    }
    std::cout << "sensor routes: list, storage, alerts, hold, edit and retirement\n";
}
