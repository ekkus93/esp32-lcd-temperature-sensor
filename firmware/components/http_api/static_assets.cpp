// The web app's files and the captive-portal probes (moved from HttpApi::handle, G10).
#include "static_assets.hpp"
namespace monitor {
std::optional<esp_err_t> answer_portal_probe(HttpExchange &ex, const std::string &path,
                                             NetworkPort &network) {
    const bool portal_probe = path == "/generate_204" || path == "/gen_204" ||
                              path == "/hotspot-detect.html" || path == "/connecttest.txt" ||
                              path == "/ncsi.txt";
    if (!portal_probe || ex.method() != HttpMethod::get || !network.info().provisioning)
        return std::nullopt;
    // Only OS connectivity probes bypass Host checking; APIs never do.
    ex.set_status("302 Found");
    ex.set_header("Location", "http://192.168.4.1/setup");
    ex.set_header("Cache-Control", "no-store");
    return ex.send("", 0);
}
bool is_app_page(const std::string &path) {
    return path == "/" || path == "/history" || path == "/sensors" || path == "/alerts" ||
           path == "/events" || path == "/settings" || path == "/setup";
}
esp_err_t send_static_file(HttpExchange &ex, const char *path, const char *type,
                           const unsigned char *data, std::size_t size) {
    ex.set_type(type);
    ex.set_header("Content-Encoding", "gzip");
    ex.set_header("Vary", "Accept-Encoding");
    ex.set_header("Cache-Control", std::strcmp(path, "/index.html") == 0
                                       ? "no-cache"
                                       : "public, max-age=31536000, immutable");
    ex.set_header("X-Content-Type-Options", "nosniff");
    ex.set_header(
        "Content-Security-Policy",
        "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src "
        "'self' ws:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");
    const esp_err_t sent = ex.send(reinterpret_cast<const char *>(data), size);
    // The browser fetches the page's files in parallel over at most six connections, and each
    // keep-alive connection holds one of max_open_sockets. Closing after an asset frees its socket
    // at once, so the live WebSocket and API calls are not purged to make room.
    ex.close_connection();
    return sent;
}
} // namespace monitor
