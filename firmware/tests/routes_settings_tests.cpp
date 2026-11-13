// Device settings routes over fakes (God file refactor G06): public and owner reads, and every
// refusal and outcome of a PATCH.
#include "fake_exchange.hpp"
#include "fake_ports.hpp"
#include "routes_settings.hpp"
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
const std::optional<Session> owner = Session{"token", "csrf", 3600000};
FakeExchange settings_request(HttpMethod method, const std::string &body = "",
                              const char *match = nullptr) {
    FakeExchange ex;
    ex.request_method = method;
    ex.request_uri = "/api/v1/settings";
    ex.body = body;
    if (!body.empty())
        ex.request_headers["Content-Type"] = "application/json";
    if (match)
        ex.request_headers["If-Match"] = match;
    return ex;
}
void run(FakeExchange &ex, FakeDevice &device, const std::optional<Session> &session) {
    const std::string path = "/api/v1/settings";
    RouteRequest request{ex, path, session, 0};
    route_settings(request, device.ports);
}
const char *current = "\"0123456789abcdef:settings:1\"";
} // namespace

int main() {
    // GET: display fields for anyone; etag, revision, writable and the NTP server for the owner.
    {
        FakeDevice device;
        auto anonymous = settings_request(HttpMethod::get);
        run(anonymous, device, none);
        CHECK(anonymous.status == "200 OK");
        CHECK(anonymous.sent.find("\"units\":\"F\"") != std::string::npos);
        for (const char *hidden : {"etag", "revision", "writable", "ntp_server"})
            CHECK(anonymous.sent.find(std::string("\"") + hidden + "\"") == std::string::npos);
        auto signed_in = settings_request(HttpMethod::get);
        run(signed_in, device, owner);
        CHECK(signed_in.sent.find(R"("etag":"\"0123456789abcdef:settings:1\"")") !=
              std::string::npos);
        CHECK(signed_in.sent.find("\"revision\":1,\"writable\":true") != std::string::npos);
        CHECK(signed_in.sent.find("\"ntp_server\":") != std::string::npos);
        device.settings.available = false;
        auto busy = settings_request(HttpMethod::get);
        run(busy, device, owner);
        CHECK(busy.status == "503 Service Unavailable" &&
              busy.sent.find("Settings busy") != std::string::npos);
    }
    // PATCH refusals: none of them saves.
    {
        FakeDevice device;
        device.settings.writable = false;
        auto damaged = settings_request(HttpMethod::patch, R"({"units":"C"})", current);
        run(damaged, device, owner);
        CHECK(damaged.status == "503 Service Unavailable");
        device.settings.writable = true;
        auto no_match = settings_request(HttpMethod::patch, R"({"units":"C"})");
        run(no_match, device, owner);
        CHECK(no_match.status == "428 Precondition Required");
        auto stale = settings_request(HttpMethod::patch, R"({"units":"C"})", "\"x:settings:0\"");
        run(stale, device, owner);
        CHECK(stale.status == "412 Precondition Failed");
        auto empty = settings_request(HttpMethod::patch, "", current);
        run(empty, device, owner);
        CHECK(empty.status == "400 Bad Request");
        for (const char *invalid :
             {"{}", R"({"units":"K"})", R"({"display_mode":"fancy"})", R"({"units":1})",
              R"({"overview_seconds":-1})", R"({"overview_seconds":1441})",
              R"({"trends_seconds":2.5})", R"({"color":"red"})", R"({"units":"C","units":"F"})"}) {
            auto ex = settings_request(HttpMethod::patch, invalid, current);
            run(ex, device, owner);
            CHECK(ex.status == "400 Bad Request");
            CHECK(ex.sent == R"({"error":"Invalid device settings"})");
        }
        CHECK(device.side_effects() == 0);
    }
    // PATCH outcomes: saved, then the hostname and clock follow and the stored settings answer.
    {
        FakeDevice device;
        auto saved = settings_request(
            HttpMethod::patch,
            R"({"units":"C","display_mode":"extended","overview_seconds":30,"hostname":"garage"})",
            current);
        run(saved, device, owner);
        CHECK(saved.status == "200 OK");
        CHECK(device.settings.saved && !device.settings.saved->fahrenheit);
        CHECK(device.settings.saved->extended && device.settings.saved->overview_seconds == 30);
        CHECK(device.host.hostname == "garage" && device.system.log.count("configure_clock") == 1);
        CHECK(saved.sent.find(R"("etag":"\"0123456789abcdef:settings:2\"")") != std::string::npos);
        CHECK(saved.sent.find("\"units\":\"C\"") != std::string::npos);
        device.settings.save_result = ESP_FAIL;
        auto failed = settings_request(HttpMethod::patch, R"({"units":"F"})",
                                       "\"0123456789abcdef:settings:2\"");
        run(failed, device, owner);
        CHECK(failed.sent == R"({"error":"Settings could not be committed"})");
        CHECK(device.host.log.count("apply_hostname") == 1);
        device.settings.save_result = ESP_OK;
        device.system.clock_accepts = false;
        auto clock = settings_request(HttpMethod::patch, R"({"units":"F"})",
                                      "\"0123456789abcdef:settings:2\"");
        run(clock, device, owner);
        CHECK(clock.sent == R"({"error":"Settings saved; clock configuration unavailable"})");
    }
    std::cout << "settings routes: public and owner reads, PATCH refusals and outcomes\n";
}
