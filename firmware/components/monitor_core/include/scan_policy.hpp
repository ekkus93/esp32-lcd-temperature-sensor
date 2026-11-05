#pragma once
#include "monitor_types.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace monitor {
// What the 1-Wire driver reported for one ROM search.
struct ScanInput {
    bool iterator_created = true;   // false if the search could not even start
    std::vector<uint64_t> observed; // raw ROMs returned, valid or not
    bool ended_normally = false;    // the driver reported "no more devices"
    bool capped = false;            // the 16-device bound was reached with the search unfinished
};

// Decides when to rescan the bus, which ROMs to read and when the shared bus is faulty. It holds
// no ESP-IDF calls, so the service around it stays thin and these rules run on the host.
class ScanPolicy {
  public:
    // Start of a read cycle; `line_low` is the sampled state of the data line. Returns false if
    // the cycle must stop because the line itself is faulty.
    bool begin_cycle(bool line_low);
    bool scan_due(int64_t now_ms) const {
        return now_ms >= next_scan_;
    }
    // Applies a search result. Returns false if no reads should be attempted this cycle.
    bool apply_scan(const ScanInput &input, int64_t now_ms, const std::vector<uint64_t> &managed);
    // The reset, the Convert T write or the completion check failed.
    void transaction_failed(int64_t now_ms);
    // After the addressed reads: how many of the expected probes answered, and whether any value
    // was missing or invalid.
    void finish_reads(int64_t now_ms, std::size_t completed, bool any_value_missing);

    bool has_probes() const {
        return !roms_.empty();
    }
    const std::vector<uint64_t> &roms() const {
        return roms_;
    }
    const ProbeScanStatus &status() const {
        return status_;
    }
    bool fault() const {
        return fault_;
    }

  private:
    std::vector<uint64_t> roms_;
    // A smaller-than-expected scan waiting for a second scan to agree with it.
    std::optional<std::vector<uint64_t>> suspect_;
    int64_t next_scan_ = 0;
    // Set while scans find no device at all although several probes are being read; kept across
    // read cycles so the fault does not flicker between rescans.
    bool no_presence_ = false;
    int64_t no_presence_since_ = 0;
    bool fault_ = false;
    ProbeScanStatus status_{};
};
} // namespace monitor
