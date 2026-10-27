#pragma once
#include "alert_service.hpp"
#include <optional>
#include <vector>

namespace monitor {
class Registry {
  public:
    bool discover(uint64_t rom);
    bool configure(uint64_t rom, const Settings &settings,
                   std::optional<int64_t> now_ms = std::nullopt);
    void sample(const std::vector<Reading> &readings, int64_t now_ms, bool bus_fault = false);
    void expire(int64_t now_ms);
    const std::vector<Sensor> &sensors() const;
    const std::vector<Event> &events() const;
    uint64_t sequence() const;
    unsigned excess_count() const;
    bool bus_fault() const;

  private:
    Sensor *find(uint64_t rom);
    void transition(Sensor &sensor, State state, int64_t now_ms);
    void record(Event event);
    void event(uint64_t rom, State previous, State state, int64_t now_ms);
    AlertService alerts_;
    std::vector<Sensor> sensors_;
    std::vector<Event> events_;
    uint64_t sequence_ = 0;
    uint64_t event_sequence_ = 0;
    unsigned excess_ = 0;
    bool bus_fault_ = false;
};
} // namespace monitor
