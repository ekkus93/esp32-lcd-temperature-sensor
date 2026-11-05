#include "storage_error_limiter.hpp"
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
int main() {
    constexpr int64_t bucket = 300000, hour = StorageErrorLimiter::reminder_ms;
    {
        // A persistent fault: one event at the start, then one per hour, not one per bucket.
        StorageErrorLimiter limiter;
        CHECK(!limiter.should_record(true, 0));
        int recorded = 0;
        for (int i = 1; i <= 288; ++i) // 24 hours of failing buckets
            recorded += limiter.should_record(false, i * bucket);
        CHECK(recorded == 24);
    }
    {
        // A healthy write ends the episode; the next failure is reported at once.
        StorageErrorLimiter limiter;
        CHECK(limiter.should_record(false, 1000));
        CHECK(!limiter.should_record(false, 1000 + bucket));
        CHECK(!limiter.should_record(true, 1000 + 2 * bucket));
        CHECK(limiter.should_record(false, 1000 + 3 * bucket));
    }
    {
        // The reminder fires exactly at one hour, not before.
        StorageErrorLimiter limiter;
        CHECK(limiter.should_record(false, 5000));
        CHECK(!limiter.should_record(false, 5000 + hour - 1));
        CHECK(limiter.should_record(false, 5000 + hour));
    }
    {
        // A clock that goes backwards does not suppress reports forever.
        StorageErrorLimiter limiter;
        CHECK(limiter.should_record(false, 10 * hour));
        CHECK(limiter.should_record(false, 5 * hour));
    }
    std::cout << "PASS: storage errors are journaled once per episode plus hourly reminders\n";
}
