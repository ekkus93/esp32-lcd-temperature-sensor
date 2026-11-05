#include "scan_policy.hpp"
#include "probe_discovery.hpp"
#include <algorithm>

namespace monitor {
namespace {
constexpr int64_t scan_interval_ms = 30000;
constexpr int64_t retry_interval_ms = 5000;
} // namespace

bool ScanPolicy::begin_cycle(bool line_low) {
    fault_ = bus_fault_detected(line_low, false, 0, 0);
    return !fault_;
}

bool ScanPolicy::apply_scan(const ScanInput &input, int64_t now_ms,
                            const std::vector<uint64_t> &managed) {
    next_scan_ = now_ms + scan_interval_ms;
    if (!input.iterator_created) {
        suspect_.reset();
        status_ = probe_scan_status(0, false, false);
        fault_ = bus_fault_detected(false, true, roms_.size(), 0);
        next_scan_ = now_ms + retry_interval_ms;
        return false;
    }
    const auto discovery =
        collect_probe_discovery(input.observed, input.ended_normally, input.capped);
    status_ = discovery.status;
    if (status_.complete) {
        // The driver ends a search with the same "no more devices" for a real end and for a glitch
        // mid-search, so a scan that lost a probe being read is only believed once a second scan
        // agrees. Until then the old list keeps being read and nothing can be retired on it.
        const bool lost_a_probe = std::any_of(roms_.begin(), roms_.end(), [&](uint64_t rom) {
            return std::find(discovery.roms.begin(), discovery.roms.end(), rom) ==
                   discovery.roms.end();
        });
        if (lost_a_probe && !(suspect_ && *suspect_ == discovery.roms)) {
            suspect_ = discovery.roms;
            status_.complete = false;
            next_scan_ = now_ms + retry_interval_ms;
            return true;
        }
        suspect_.reset();
        roms_ = select_probe_reads(discovery.roms, managed);
        return true;
    }
    suspect_.reset();
    next_scan_ = now_ms + retry_interval_ms;
    if (!input.capped) {
        fault_ = bus_fault_detected(false, true, roms_.size(), 0);
        return false;
    }
    auto candidates = discovery.roms;
    candidates.insert(candidates.end(), roms_.begin(), roms_.end());
    roms_ = select_probe_reads(candidates, managed);
    return true;
}

void ScanPolicy::transaction_failed(int64_t now_ms) {
    fault_ = bus_fault_detected(false, true, roms_.size(), 0);
    next_scan_ = now_ms + retry_interval_ms;
}

void ScanPolicy::finish_reads(int64_t now_ms, std::size_t completed, bool any_value_missing) {
    if (any_value_missing || (!roms_.empty() && completed == 0))
        next_scan_ = now_ms + retry_interval_ms;
    // No addressed transaction completed: all expected probes failed together, which is one
    // shared-bus fault rather than one failure per sensor.
    fault_ = bus_fault_detected(false, false, roms_.size(), completed);
}
} // namespace monitor
