#pragma once
// The web app's files and the captive-portal probes (God file refactor G10).
#include "api_ports.hpp"
#include "api_responses.hpp"
#include <cstring>
#include <optional>
#include <string>
namespace monitor {
// While the setup access point is open, an OS connectivity probe (Android, Apple, Windows) is sent
// to the setup page; nothing else bypasses the Host check. Returns the answer, or nothing when
// `path` is not a probe or setup is not open (the network is asked only for a probe).
std::optional<esp_err_t> answer_portal_probe(HttpExchange &exchange, const std::string &path,
                                             NetworkPort &network);
// Sends one embedded file (gzip) with its type and cache, security and close headers.
esp_err_t send_static_file(HttpExchange &exchange, const char *path, const char *type,
                           const unsigned char *data, std::size_t size);
// The app's own pages, which all load index.html.
bool is_app_page(const std::string &path);
// Answers a GET for a file in `assets` (anything with path, type, data and size: the generated
// web_assets table on the device, a small table in tests), or for an app page with index.html.
template <class Asset>
esp_err_t serve_static_asset(HttpExchange &ex, const std::string &path, const Asset *assets,
                             std::size_t count) {
    if (ex.method() != HttpMethod::get)
        return send_error(ex, "405 Method Not Allowed", "Method not allowed");
    const Asset *asset = nullptr;
    for (std::size_t i = 0; i < count && !asset; ++i)
        if (path == assets[i].path)
            asset = &assets[i];
    if (!asset) {
        if (!is_app_page(path))
            return send_error(ex, "404 Not Found", "Not found");
        for (std::size_t i = 0; i < count && !asset; ++i)
            if (std::strcmp(assets[i].path, "/index.html") == 0)
                asset = &assets[i];
    }
    if (!asset)
        return ESP_FAIL;
    if (ex.header("Accept-Encoding", 256).find("gzip") == std::string::npos)
        return send_error(ex, "406 Not Acceptable", "Client must accept gzip");
    return send_static_file(ex, asset->path, asset->type, asset->data, asset->size);
}
} // namespace monitor
