#include "history.hpp"
#include "monitor.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
namespace {
uint64_t rom(unsigned serial) {
    uint64_t value = 0x28 | (uint64_t(serial) << 8);
    std::array<uint8_t, 8> bytes{};
    for (unsigned i = 0; i < 7; ++i)
        bytes[i] = (value >> (8 * i)) & 0xff;
    return value | (uint64_t(crc8(bytes.data(), 7)) << 56);
}
// Runs one full five-minute bucket of `readings` (one per 5 s) for a probe with the given
// calibration and returns the finished bucket.
std::optional<Bucket> one_bucket(const std::vector<double> &readings, double calibration) {
    Registry registry;
    Aggregator aggregator(1);
    const auto id = rom(1);
    registry.sample({{id, readings[0]}}, 0);
    auto settings = registry.sensors()[0].settings;
    settings.calibration = calibration;
    CHECK(registry.configure(id, settings));
    int64_t now = 0;
    for (size_t i = 0; i < 60; ++i, now += 5000) {
        registry.sample({{id, readings[i % readings.size()]}}, now);
        CHECK(!aggregator.add(registry, now));
    }
    registry.sample({{id, readings[0]}}, now);
    return aggregator.add(registry, now);
}
} // namespace
int main() {
    // A steady probe with any calibration offset must still produce a bucket that encodes: the
    // average of equal values can land one rounding step outside [min, max] in doubles.
    unsigned checked = 0;
    for (double calibration : {0.0, 0.1, 0.2, -0.3, 0.05, 1.7, -2.45, 12.7, -12.7}) {
        for (int step = -200; step < 400; step += 7) {
            const double raw = step * 0.0625;
            if (raw + calibration < -55 || raw + calibration > 125)
                continue;
            const auto bucket = one_bucket({raw}, calibration);
            CHECK(bucket && bucket->sensors.size() == 1);
            const auto &sensor = bucket->sensors[0];
            CHECK(sensor.minimum <= sensor.average && sensor.average <= sensor.maximum);
            const auto encoded = encode_bucket(*bucket);
            CHECK(!encoded.empty());
            const auto decoded = decode_bucket(encoded);
            CHECK(decoded && decoded->sensors.size() == 1);
            CHECK(std::abs(decoded->sensors[0].average - sensor.average) < 0.001);
            ++checked;
        }
    }
    CHECK(checked > 400);

    // Varying readings keep a true average strictly between min and max.
    const auto varying = one_bucket({10.0, 10.5, 11.0, 10.25}, 0.1);
    CHECK(varying && !encode_bucket(*varying).empty());
    CHECK(varying->sensors[0].minimum < varying->sensors[0].average);
    CHECK(varying->sensors[0].average < varying->sensors[0].maximum);

    // Genuinely inconsistent data is still rejected by the encoder.
    auto bad = *varying;
    bad.sensors[0].average = bad.sensors[0].maximum + 0.5;
    CHECK(encode_bucket(bad).empty());
    bad = *varying;
    bad.sensors[0].average = bad.sensors[0].minimum - 0.5;
    CHECK(encode_bucket(bad).empty());
    std::cout << "PASS: calibrated buckets encode, true inconsistencies are still rejected\n";
}
