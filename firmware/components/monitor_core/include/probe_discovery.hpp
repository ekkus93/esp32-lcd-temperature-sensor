#pragma once
#include "monitor_types.hpp"
#include <vector>

namespace monitor {
struct ProbeDiscovery {
    std::vector<uint64_t> roms;
    ProbeScanStatus status;
};
ProbeDiscovery collect_probe_discovery(const std::vector<uint64_t> &observed,
                                       bool iterator_complete, bool enumeration_capped);
ProbeScanStatus probe_scan_status(std::size_t unique_valid_roms, bool iterator_complete,
                                  bool enumeration_capped);
std::vector<uint64_t> select_probe_reads(const std::vector<uint64_t> &discovered,
                                         const std::vector<uint64_t> &managed);
} // namespace monitor
