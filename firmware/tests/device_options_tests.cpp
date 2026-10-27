#include "device_options.hpp"
#include <cstdlib>
#include <ctime>
#include <iostream>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
int main() {
    DeviceOptions options;
    CHECK(valid_device_options(options));
    CHECK(!options.extended && options.overview_seconds == 20 && options.trends_seconds == 10);
    options.extended = true;
    options.fahrenheit = true;
    options.timezone = "America/Los_Angeles";
    options.ntp_server = "time.example.org";
    const auto bytes = encode_device_options(options);
    const auto restored = decode_device_options(bytes);
    CHECK(restored && restored->fahrenheit && restored->extended &&
          restored->timezone == options.timezone && restored->ntp_server == options.ntp_server);
    for (size_t i = 0; i < bytes.size(); ++i) {
        auto bad = bytes;
        bad[i] ^= 1;
        CHECK(!decode_device_options(bad));
        CHECK(!decode_device_options(bytes.substr(0, i)));
    }
    auto newer = bytes;
    newer[3] = '2';
    CHECK(!decode_device_options(newer));
    for (const auto &server :
         {"http://time.example.org", "-bad.example", "bad-.example", "bad..example", "a b", ""}) {
        auto bad = options;
        bad.ntp_server = server;
        CHECK(!valid_device_options(bad));
    }
    options.timezone = "not-a-timezone";
    CHECK(!valid_device_options(options));
    options.timezone = "UTC";
    options.overview_seconds = 301;
    CHECK(!valid_device_options(options));
    // Verify DST using libc under the same explicit POSIX rules sent to newlib.
    struct ZoneCase {
        const char *id;
        int winter_hour, summer_hour;
    };
    const ZoneCase zones[] = {{"UTC", 12, 12},
                              {"America/Los_Angeles", 4, 5},
                              {"America/New_York", 7, 8},
                              {"Europe/London", 12, 13},
                              {"Europe/Berlin", 13, 14},
                              {"Asia/Tokyo", 21, 21},
                              {"Australia/Sydney", 23, 22}};
    for (const auto &zone : zones) {
        CHECK(setenv("TZ", timezone_rule(zone.id), 1) == 0);
        tzset();
        std::time_t winter = 1768478400; // 2026-01-15 12:00 UTC
        std::time_t summer = 1784116800; // 2026-07-15 12:00 UTC
        struct tm w {
        }, s{};
        localtime_r(&winter, &w);
        localtime_r(&summer, &s);
        CHECK(w.tm_hour == zone.winter_hour && s.tm_hour == zone.summer_hour);
    }
    DisplayCycle cycle;
    CHECK(cycle.configure(true, 20, 10, 0));
    CHECK(cycle.trends(25000)); // Local timezone changes do not enter monotonic timing.
    CHECK(cycle.configure(true, 30, 15, 20000)); // Runtime settings restart the monotonic cycle.
    CHECK(!cycle.trends(49999) && cycle.trends(50000));
    CHECK(!cycle.configure(true, 4, 15, 50000)); // Bounds reject invalid dwell durations.
    std::cout
        << "PASS: versioned device settings, validation, corruption, defaults and DST rules\n";
}
