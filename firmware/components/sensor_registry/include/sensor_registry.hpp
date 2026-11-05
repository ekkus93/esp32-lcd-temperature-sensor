#pragma once
#include "alert_service.hpp"
#include <optional>
#include <vector>

namespace monitor {
class Registry {
  public:
    bool discover(uint64_t rom);
    bool retire(uint64_t rom);
    bool configure(uint64_t rom, const Settings &settings,
                   std::optional<int64_t> now_ms = std::nullopt);
    // `monitored` is false when this build cannot read probes at all (the sensor bus is compiled
    // out): saved probes then show as disabled instead of raising disconnect alarms.
    void sample(const std::vector<Reading> &readings, int64_t now_ms, bool bus_fault = false,
                bool monitored = true);
    void expire(int64_t now_ms);
    const std::vector<Sensor> &sensors() const;
    const std::vector<Event> &events() const;
    uint64_t sequence() const;
    bool bus_fault() const;

  private:
    Sensor *find(uint64_t rom);
    void transition(Sensor &sensor, State state, int64_t now_ms);
    void record(Event event);
    void event(uint64_t rom, State previous, State state, int64_t now_ms);
    struct Recorded {
        uint64_t rom;
        State previous;
        State state;
        int64_t uptime_ms;
    };
    // The last journaled transition of each kind; an identical one inside this window is not
    // journaled again (the live state still changes), so a flapping sensor cannot flush weeks of
    // alarm history out of the 128-entry journal.
    static constexpr int64_t journal_repeat_ms = 60000;
    static constexpr std::size_t max_recorded = 32;
    std::vector<Recorded> recorded_;
    AlertService alerts_;
    std::vector<Sensor> sensors_;
    std::vector<Event> events_;
    uint64_t sequence_ = 0;
    uint64_t event_sequence_ = 0;
    bool bus_fault_ = false;
};
} // namespace monitor
