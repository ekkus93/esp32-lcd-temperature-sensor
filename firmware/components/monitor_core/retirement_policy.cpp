#include "retirement_policy.hpp"
#include <algorithm>

namespace monitor {
bool can_retire_sensor(State state, uint64_t rom, const ProbeScanStatus &scan, bool simulated) {
    if (state != State::disconnected)
        return false;
    return simulated ||
           (scan.complete && std::find(scan.discovered_roms.begin(), scan.discovered_roms.end(),
                                       rom) == scan.discovered_roms.end());
}
} // namespace monitor
