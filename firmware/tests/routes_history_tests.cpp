// History routes over fakes (God file refactor G09): admission and limits on the HTTP task, and
// the worker's chart and CSV answers, including incomplete exports.
#include "fake_exchange.hpp"
#include "fake_ports.hpp"
#include "routes_history.hpp"
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
const std::optional<Session> none;
const char *range = "from=1800000000000&to=1800003600000";
FakeExchange admit(FakeDevice &device, const std::string &path, const std::string &query,
                   bool async = true) {
    FakeExchange ex;
    ex.request_uri = path + (query.empty() ? "" : "?" + query);
    ex.async_available = async;
    RouteRequest request{ex, path, none, 0};
    route_history(request, device.ports);
    return ex;
}
uint64_t probe() {
    uint8_t bytes[7] = {0x28, 1};
    return 0x28 | uint64_t(1) << 8 | uint64_t(crc8(bytes, 7)) << 56;
}
Bucket bucket(int64_t index) {
    Bucket value{7, index * 300000, 1800000000 + index * 300, {{probe(), 4, 3, 5, 60, false}}};
    value.sensors[0].name = "Canopy";
    return value;
}
HistoryQuery query() {
    HistoryQuery value;
    value.from = 1800000000000;
    value.to = 1800003600000;
    return value;
}
} // namespace

int main() {
    // Admission on the HTTP task.
    {
        FakeDevice device;
        CHECK(admit(device, "/api/v1/history", "").status == "400 Bad Request");
        CHECK(admit(device, "/api/v1/history", "from=" + std::string(260, '1')).status ==
              "400 Bad Request");
        CHECK(admit(device, "/api/v1/history", "from=5&to=1").sent ==
              R"({"error":"Invalid history bounds or selector"})");
        CHECK(admit(device, "/api/v1/history/export", std::string(range) + "&max_points=10").sent ==
              R"({"error":"CSV exports full five-minute records; omit max_points"})");
        CHECK(device.host.log.count("admit_public") == 0);
        // Over the public limit: 429 with the wait rounded up to whole seconds.
        device.host.admission = {false, 29001};
        auto limited = admit(device, "/api/v1/history/export", range);
        CHECK(limited.status == "429 Too Many Requests" &&
              limited.response_header("Retry-After") == "30");
        device.host.admission = {true, 0};
        // Admitted but no async answer or no worker slot: the admission is released, 503.
        auto no_async = admit(device, "/api/v1/history", range, false);
        CHECK(no_async.status == "503 Service Unavailable");
        device.host.accept = false;
        auto full = admit(device, "/api/v1/history", range);
        CHECK(full.status == "503 Service Unavailable");
        CHECK(device.host.finished.size() == 2 && device.host.admitted.size() == 2);
        // Queued: nothing is answered here; the slot stays held for the worker.
        device.host.accept = true;
        auto queued = admit(device, "/api/v1/history/export", range);
        CHECK(queued.sends == 0 && queued.chunks.empty());
        CHECK(device.host.last_exporting && device.host.last_query->to == 1800003600000);
        CHECK(device.host.admitted.back() == PublicRequest::export_csv);
        CHECK(device.host.finished.size() == 2);
    }
    // The worker's chart answer.
    {
        FakeDevice device;
        FakeExchange unavailable;
        device.history.available = false;
        serve_history(unavailable, device.ports, query(), false);
        CHECK(unavailable.sent == R"({"error":"History unavailable or busy"})");
        device.history.available = true;
        device.history.truncated = true;
        FakeExchange chart;
        CHECK(serve_history(chart, device.ports, query(), false) == ESP_OK);
        CHECK(chart.type == "application/json" &&
              chart.response_header("Cache-Control") == "no-store");
        CHECK(chart.sent == R"({"schema_version":1,"simulated":false,"truncated":true,)"
                            R"("time_basis":"utc","points":[],"gaps":[]})");
    }
    // The worker's CSV answer: complete, refused before any row, and aborted after rows.
    {
        FakeDevice device;
        device.history.buckets = {bucket(0), bucket(1)};
        FakeExchange complete;
        CHECK(serve_history(complete, device.ports, query(), true) == ESP_OK);
        CHECK(complete.type == "text/csv; charset=utf-8" && complete.chunked_finished);
        CHECK(complete.response_header("Content-Disposition") ==
              "attachment; filename=temperature-history.csv");
        CHECK(complete.chunks.size() == 2 && complete.chunks[0].rfind("schema_version,", 0) == 0);
        CHECK(complete.chunks[1].find("\"Canopy\"") != std::string::npos);

        device.history.buckets.clear();
        device.history.visit_end = HistoryVisitEnd::skipped_damaged;
        FakeExchange damaged;
        serve_history(damaged, device.ports, query(), true);
        CHECK(damaged.status == "503 Service Unavailable" && damaged.chunks.empty());
        CHECK(damaged.sent.find("damaged") != std::string::npos);
        device.history.visit_end = HistoryVisitEnd::deadline;
        FakeExchange slow;
        serve_history(slow, device.ports, query(), true);
        CHECK(slow.sent == R"({"error":"The export took too long. Try a shorter range."})");

        device.history.buckets = {bucket(0)};
        device.history.visit_end = HistoryVisitEnd::storage_failed;
        FakeExchange aborted;
        CHECK(serve_history(aborted, device.ports, query(), true) == ESP_FAIL);
        CHECK(!aborted.chunks.empty() && !aborted.chunked_finished && aborted.closed);
    }
    std::cout << "history routes: admission, limits, chart answer and CSV outcomes\n";
}
