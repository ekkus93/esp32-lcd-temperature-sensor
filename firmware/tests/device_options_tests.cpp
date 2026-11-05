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
    CHECK(options.timezone == "America/Los_Angeles");
    auto saved_utc = options;
    saved_utc.timezone = "UTC";
    const auto restored_utc = decode_device_options(encode_device_options(saved_utc));
    CHECK(restored_utc && restored_utc->timezone == "UTC");
    CHECK(!options.extended && options.overview_seconds == 20 && options.trends_seconds == 10);
    CHECK(options.fahrenheit); // new installs default to Fahrenheit
    auto saved_celsius = options;
    saved_celsius.fahrenheit = false;
    const auto restored_celsius = decode_device_options(encode_device_options(saved_celsius));
    CHECK(restored_celsius && !restored_celsius->fahrenheit); // saved Celsius is preserved
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
    newer[3] = '3'; // a future format this firmware does not know
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
    // The hostname is one lowercase RFC 1123 label and defaults to "temperature-monitor".
    {
        DeviceOptions named;
        CHECK(named.hostname == "temperature-monitor");
        for (const char *good : {"temperature-monitor", "a", "freezer-1", "x9", "0abc",
                                 "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz0123456789a"})
            CHECK(valid_hostname(good));
        // 64 characters, empty, edges, uppercase, dots, underscores, spaces, non-ASCII.
        for (const char *bad :
             {"", "-a", "a-", "-", "Freezer", "A", "a.b", "a_b", "a b", "temperature-monitor.local",
              "caf\xc3\xa9", "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz0123456789ab"}) {
            CHECK(!valid_hostname(bad));
            auto bad_options = named;
            bad_options.hostname = bad;
            CHECK(!valid_device_options(bad_options));
        }
        named.hostname = "freezer-1";
        const auto round_trip = decode_device_options(encode_device_options(named));
        CHECK(round_trip && round_trip->hostname == "freezer-1");
        CHECK(encode_device_options(named).rfind("TMD2 ", 0) == 0);
    }
    // A saved TMD1 record (before the hostname existed) still loads, with the default hostname,
    // and every other saved choice is kept.
    {
        const std::string legacy_body =
            "TMD1 0 1 30 15 20 \"Europe/Berlin\" \"time.example.org\"\n";
        uint32_t sum = 2166136261u;
        for (unsigned char c : legacy_body) {
            sum ^= c;
            sum *= 16777619u;
        }
        const auto migrated = decode_device_options(legacy_body + std::to_string(sum));
        CHECK(migrated);
        CHECK(!migrated->fahrenheit && migrated->extended && migrated->overview_seconds == 30 &&
              migrated->trends_seconds == 15 && migrated->lookback_minutes == 20 &&
              migrated->timezone == "Europe/Berlin" && migrated->ntp_server == "time.example.org");
        CHECK(migrated->hostname == "temperature-monitor");
        // Re-saving writes the new format.
        CHECK(encode_device_options(*migrated).rfind("TMD2 ", 0) == 0);
        // A damaged TMD1 record is still refused.
        auto damaged = legacy_body + std::to_string(sum + 1);
        CHECK(!decode_device_options(damaged));
    }
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
