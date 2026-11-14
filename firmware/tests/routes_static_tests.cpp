// Static files and captive-portal probes over fakes (God file refactor G10).
#include "fake_exchange.hpp"
#include "fake_ports.hpp"
#include "static_assets.hpp"
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
struct Asset {
    const char *path;
    const char *type;
    const unsigned char *data;
    std::size_t size;
};
const unsigned char page[] = {'<', 'p', '>'};
const unsigned char script[] = {'j', 's'};
const Asset assets[] = {{"/index.html", "text/html; charset=utf-8", page, sizeof(page)},
                        {"/assets/app-1.js", "text/javascript", script, sizeof(script)}};
FakeExchange get(const std::string &path, bool gzip = true, HttpMethod method = HttpMethod::get) {
    FakeExchange ex;
    ex.request_method = method;
    ex.request_uri = path;
    if (gzip)
        ex.request_headers["Accept-Encoding"] = "gzip, deflate";
    serve_static_asset(ex, path, assets, 2);
    return ex;
}
} // namespace

int main() {
    // A hashed asset: cached for a year, gzip, security headers, connection closed after.
    auto js = get("/assets/app-1.js");
    CHECK(js.status == "200 OK" && js.type == "text/javascript" && js.sent == "js");
    CHECK(js.response_header("Content-Encoding") == "gzip");
    CHECK(js.response_header("Vary") == "Accept-Encoding");
    CHECK(js.response_header("Cache-Control") == "public, max-age=31536000, immutable");
    CHECK(js.response_header("X-Content-Type-Options") == "nosniff");
    CHECK(js.response_header("Content-Security-Policy").find("frame-ancestors 'none'") !=
          std::string::npos);
    CHECK(js.closed);
    // Every app page loads index.html, revalidated each time.
    for (const char *page_path : {"/", "/history", "/sensors", "/alerts", "/events", "/settings",
                                  "/setup", "/index.html"}) {
        auto ex = get(page_path);
        CHECK(ex.sent == "<p>" && ex.response_header("Cache-Control") == "no-cache");
    }
    // Anything else is not found; other methods are not allowed; gzip is required.
    CHECK(get("/dashboard").status == "404 Not Found");
    CHECK(get("/assets/missing.js").status == "404 Not Found");
    CHECK(get("/", true, HttpMethod::post).status == "405 Method Not Allowed");
    auto plain = get("/", false);
    CHECK(plain.status == "406 Not Acceptable" && !plain.closed);
    // Without index.html in the table an app page cannot be answered.
    FakeExchange missing;
    missing.request_headers["Accept-Encoding"] = "gzip";
    CHECK(serve_static_asset(missing, "/", assets + 1, 1) == ESP_FAIL);

    // Captive-portal probes: redirected only while setup is open, only for GET, only probes.
    FakeNetwork network;
    for (const char *probe :
         {"/generate_204", "/gen_204", "/hotspot-detect.html", "/connecttest.txt", "/ncsi.txt"}) {
        FakeExchange closed;
        CHECK(!answer_portal_probe(closed, probe, network));
        network.network.provisioning = true;
        FakeExchange open;
        CHECK(answer_portal_probe(open, probe, network) == ESP_OK);
        CHECK(open.status == "302 Found" &&
              open.response_header("Location") == "http://192.168.4.1/setup");
        CHECK(open.response_header("Cache-Control") == "no-store" && open.sent.empty());
        FakeExchange post;
        post.request_method = HttpMethod::post;
        CHECK(!answer_portal_probe(post, probe, network));
        network.network.provisioning = false;
    }
    const int asked = network.log.count("info");
    FakeExchange other;
    CHECK(!answer_portal_probe(other, "/api/v1/status", network));
    CHECK(network.log.count("info") == asked); // the network is not asked for other paths
    std::cout << "static files and portal probes\n";
}
