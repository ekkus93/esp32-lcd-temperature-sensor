#pragma once
#include "history_visit.hpp"
#include "monitor.hpp"
#include <functional>
#include <vector>
namespace monitor {
const char *history_csv_header();
std::string history_csv_row(const Bucket &bucket, const Aggregate &sensor, bool simulated);

// Where an export's bytes go: the HTTP response on the device, a recorder in host tests.
class CsvExportSink {
  public:
    virtual ~CsvExportSink() = default;
    // Sends the response headers and the CSV header row.
    virtual bool begin() = 0;
    virtual bool send(const char *data, std::size_t size) = 0;
    // Sends the normal final chunk: the file is complete.
    virtual bool finish() = 0;
};
enum class CsvExportEnd : uint8_t {
    complete, // every row sent and the normal final chunk sent
    refused,  // nothing was sent: answer with an error status instead
    aborted,  // rows were sent but the file is incomplete: close without a final chunk
};
struct CsvExportResult {
    CsvExportEnd end = CsvExportEnd::refused;
    HistoryVisitOutcome visit;
};
// Runs one visit (with `stop_on_damage`, so damage stops the export at once).
using HistoryVisitFunction = std::function<HistoryVisitOutcome(
    const std::function<bool(const Bucket &)> &visitor, const HistoryVisitOptions &options)>;
// Streams the visit's records as CSV in chunks of about `chunk_bytes`. The headers are sent with
// the first row (or at the end of an empty complete export), so an export known to be incomplete
// before then is refused; one found incomplete later is aborted. An incomplete export never gets
// the normal final chunk, so neither the browser nor a script can take it for a whole file.
CsvExportResult export_history_csv(const HistoryVisitFunction &visit,
                                   const std::vector<uint64_t> &roms, bool simulated,
                                   CsvExportSink &sink, std::size_t chunk_bytes = 4096);
} // namespace monitor
