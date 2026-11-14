// History routes (moved from HttpApi::handle and HttpApi::serve_history, God file refactor G09).
#include "routes_history.hpp"
#include "api_responses.hpp"
#include "esp_log.h"
#include "history_csv.hpp"
#include "history_json.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace monitor {
esp_err_t route_history(RouteRequest &request, RoutePorts &ports) {
    auto &ex = request.ex;
    const auto query_text = url_query(ex);
    if (query_text.empty() || query_text.size() > 256)
        return send_error(ex, "400 Bad Request", "Bounded history query required");
    bool has_max_points = false;
    auto parsed_query = parse_history_query(query_text, &has_max_points);
    if (!parsed_query)
        return send_error(ex, "400 Bad Request", "Invalid history bounds or selector");
    const auto &query = *parsed_query;
    const bool exporting = request.path == "/api/v1/history/export";
    if (exporting && has_max_points)
        return send_error(ex, "400 Bad Request",
                          "CSV exports full five-minute records; omit max_points");
    // History is public, so it is rate limited per client (one query per second, one export per
    // 30 s, two at a time overall); a refused request says when to retry.
    const auto kind = exporting ? PublicRequest::export_csv : PublicRequest::history;
    const auto allowed = ports.host.admit_public(kind, ex.client());
    if (!allowed.allow) {
        char retry[24];
        std::snprintf(
            retry, sizeof(retry), "%lld",
            static_cast<long long>(std::max<int64_t>(1, (allowed.retry_after_ms + 999) / 1000)));
        ex.set_header("Retry-After", retry);
        return send_error(ex, "429 Too Many Requests", "Too many history requests; retry later");
    }
    // The query or export runs on the history worker, so this task keeps answering other requests
    // while it reads the store and sends the answer.
    auto async = ex.begin_async();
    if (!async || !ports.host.queue_history(std::move(async), query, exporting, kind)) {
        ports.host.finish_public(kind);
        return send_error(ex, "503 Service Unavailable", "History unavailable or busy");
    }
    return ESP_OK; // the history worker answers on the async copy
}
// Answers one history query or CSV export on the history worker (never on the HTTP task).
esp_err_t serve_history(HttpExchange &ex, RoutePorts &ports, const HistoryQuery &query,
                        bool exporting) {
    if (exporting) {
        // The HTTP response as the export's destination.
        struct HttpCsvSink final : CsvExportSink {
            HttpExchange &exchange;
            explicit HttpCsvSink(HttpExchange &e) : exchange(e) {}
            bool begin() override {
                exchange.set_type("text/csv; charset=utf-8");
                exchange.set_header("Content-Disposition",
                                    "attachment; filename=temperature-history.csv");
                exchange.set_header("Cache-Control", "no-store");
                const char *header = history_csv_header();
                return exchange.send_chunk(header, std::strlen(header)) == ESP_OK;
            }
            bool send(const char *data, std::size_t size) override {
                return exchange.send_chunk(data, size) == ESP_OK;
            }
            bool finish() override {
                return exchange.send_chunk(nullptr, 0) == ESP_OK;
            }
        } sink(ex);
        const auto result = export_history_csv(
            [&](const std::function<bool(const Bucket &)> &visitor,
                const HistoryVisitOptions &options) {
                return ports.history.visit(query, visitor, options);
            },
            query.roms, ports.live.simulated(), sink);
        switch (result.end) {
        case CsvExportEnd::complete:
            return ESP_OK;
        case CsvExportEnd::refused:
            switch (result.visit.end) {
            case HistoryVisitEnd::skipped_damaged:
                return send_error(
                    ex, "503 Service Unavailable",
                    "Part of the stored history in this range is damaged, so a complete "
                    "export is not possible. The chart still shows what can be read.");
            case HistoryVisitEnd::deadline:
                return send_error(ex, "503 Service Unavailable",
                                  "The export took too long. Try a shorter range.");
            default:
                return send_error(ex, "503 Service Unavailable", "History unavailable or busy");
            }
        case CsvExportEnd::aborted:
            break;
        }
        // Rows were already sent: close the connection without the final chunk, so the download
        // fails instead of looking complete.
        ESP_LOGW("http",
                 "History export incomplete (end %u, %u damaged segment(s)); connection closed",
                 unsigned(result.visit.end), unsigned(result.visit.skipped_segments));
        ex.close_connection();
        return ESP_FAIL;
    }
    std::vector<HistoryPoint> points;
    std::vector<HistoryGap> gap_records;
    bool truncated = false;
    if (!ports.history.query(query, points, gap_records, truncated))
        return send_error(ex, "503 Service Unavailable", "History unavailable or busy");
    char *text = build_history_json(points, std::move(gap_records), truncated,
                                    ports.live.simulated(), query.boot_id.has_value());
    if (!text) {
        // No schema-valid answer could be built. The reply must not need the allocator that just
        // failed, so its body is a constant.
        ex.set_status("503 Service Unavailable");
        ex.set_type("application/json");
        ex.set_header("Cache-Control", "no-store");
        constexpr char body[] = "{\"error\":\"History response memory unavailable\"}";
        return ex.send(body, sizeof(body) - 1);
    }
    ex.set_type("application/json");
    ex.set_header("Cache-Control", "no-store");
    const auto sent = ex.send(text, std::strlen(text));
    cJSON_free(text);
    return sent;
}
} // namespace monitor
