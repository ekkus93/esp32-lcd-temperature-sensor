#pragma once
#include "monitor_types.hpp"
#include "sensor_registry.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace monitor {
struct Aggregate {
    uint64_t rom;
    double average;
    double minimum;
    double maximum;
    unsigned count;
    bool partial;
    std::string name{};       // last label observed in this bucket; empty for legacy records
    uint16_t state_flags = 0; // bit per State observed, including intervals with no valid sample
};
struct Bucket {
    uint64_t boot_id;
    int64_t start_uptime_ms;
    std::optional<int64_t> utc_seconds;
    std::vector<Aggregate> sensors;
};
class Aggregator {
  public:
    explicit Aggregator(uint64_t boot_id);
    // Returns only a completed window; callers persist this, never individual readings.
    std::optional<Bucket> add(const Registry &registry, int64_t now_ms,
                              std::optional<int64_t> utc_seconds = std::nullopt);

  private:
    struct Accumulator {
        uint64_t rom;
        double sum = 0;
        double minimum = 0;
        double maximum = 0;
        unsigned count = 0;
        std::string name{};
        uint16_t state_flags = 0;
    };
    uint64_t boot_id_;
    std::optional<int64_t> start_;
    std::optional<int64_t> utc_;
    std::optional<int64_t> anchor_utc_;
    std::optional<int64_t> next_utc_boundary_;
    int64_t anchor_ms_ = 0;
    int64_t deadline_ = 0;
    std::optional<int64_t> last_sample_;
    bool partial_ = false;
    std::vector<Accumulator> accumulators_;
};
class Clock {
  public:
    void synchronize(int64_t monotonic_ms, int64_t utc_seconds);
    std::optional<int64_t> utc(int64_t monotonic_ms) const;

  private:
    std::optional<int64_t> origin_;
    int64_t utc_ = 0;
};
class BootButton {
  public:
    // Start unarmed: BOOT held across reset must first be released.
    bool update(bool pressed, int64_t now_ms);
    unsigned progress(int64_t now_ms) const;

  private:
    bool armed_ = false;
    bool raw_ = false;
    bool stable_ = false;
    bool fired_ = false;
    int64_t changed_ = 0;
    int64_t pressed_ = 0;
};
class DisplayCycle {
  public:
    bool configure(bool extended, unsigned overview_s, unsigned trends_s, int64_t now_ms);
    bool trends(int64_t now_ms) const;

  private:
    bool extended_ = false;
    unsigned overview_s_ = 20;
    unsigned trends_s_ = 10;
    int64_t start_ = 0;
};
} // namespace monitor
