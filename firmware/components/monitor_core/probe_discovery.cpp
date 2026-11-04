#include "probe_discovery.hpp"
#include <algorithm>

namespace monitor {
ProbeDiscovery collect_probe_discovery(const std::vector<uint64_t> &observed,
                                       bool iterator_complete, bool enumeration_capped) {
    ProbeDiscovery result;
    const auto count = std::min<std::size_t>(observed.size(), 16);
    result.roms.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        if (valid_rom(observed[i]))
            result.roms.push_back(observed[i]);
    std::sort(result.roms.begin(), result.roms.end());
    result.roms.erase(std::unique(result.roms.begin(), result.roms.end()), result.roms.end());
    result.status = probe_scan_status(result.roms.size(), iterator_complete,
                                      enumeration_capped || observed.size() > 16);
    result.status.discovered_roms = result.roms;
    return result;
}

ProbeScanStatus probe_scan_status(std::size_t unique_valid_roms, bool iterator_complete,
                                  bool enumeration_capped) {
    return {static_cast<unsigned>(std::min<std::size_t>(unique_valid_roms, 16)),
            unique_valid_roms > max_sensors,
            iterator_complete && !enumeration_capped,
            false,
            {}};
}

std::vector<uint64_t> select_probe_reads(const std::vector<uint64_t> &discovered,
                                         const std::vector<uint64_t> &managed) {
    std::vector<uint64_t> selected;
    selected.reserve(max_sensors);
    const auto contains = [&](uint64_t rom) {
        return std::find(discovered.begin(), discovered.end(), rom) != discovered.end();
    };
    for (const auto rom : managed) {
        if (valid_rom(rom) && contains(rom) &&
            std::find(selected.begin(), selected.end(), rom) == selected.end())
            selected.push_back(rom);
    }
    for (const auto rom : discovered) {
        if (selected.size() == max_sensors)
            break;
        if (valid_rom(rom) && std::find(selected.begin(), selected.end(), rom) == selected.end())
            selected.push_back(rom);
    }
    return selected;
}
} // namespace monitor
