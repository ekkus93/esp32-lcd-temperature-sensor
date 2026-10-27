#include "fake_bus.hpp"
#include "history_csv.hpp"
#include <iostream>
using namespace monitor;
int main() {
    const auto rom = fake_readings(1)[0].rom;
    Bucket bucket{7, 0, {}, {{rom, -10.123, -20, 5, 60, false, "Cold, \"A\"", 1}}};
    std::cout << history_csv_header();
    auto &sensor = bucket.sensors[0];
    std::cout << history_csv_row(bucket, sensor, true);
    sensor.name = " =1+1";
    std::cout << history_csv_row(bucket, sensor, true);
    sensor.name.clear();
    sensor.state_flags = 0;
    std::cout << history_csv_row(bucket, sensor, true);
    sensor = {rom, 0, 0, 0, 0, true, "Disabled", 16};
    std::cout << history_csv_row(bucket, sensor, true);
    sensor = {rom, 20, 19, 21, 60, false, "Café", 1};
    bucket.utc_seconds = 1800000000;
    std::cout << history_csv_row(bucket, sensor, true);
}
