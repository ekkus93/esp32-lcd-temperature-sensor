#include "public_limiter.hpp"
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
    constexpr auto history = PublicRequest::history;
    constexpr auto export_csv = PublicRequest::export_csv;
    {
        // History: one query per second per client; the answer says how long to wait.
        PublicLimiter limiter;
        CHECK(limiter.begin(history, 1, 10000).allow);
        limiter.finish(history);
        auto early = limiter.begin(history, 1, 10400);
        CHECK(!early.allow && early.retry_after_ms == 600);
        CHECK(limiter.begin(history, 1, 11000).allow); // exactly one second later
        limiter.finish(history);
        // Another client is not slowed by the first.
        CHECK(limiter.begin(history, 2, 10401).allow);
        limiter.finish(history);
    }
    {
        // CSV export: one per 30 s per client; a history query is separate.
        PublicLimiter limiter;
        CHECK(limiter.begin(export_csv, 1, 0).allow);
        limiter.finish(export_csv);
        auto again = limiter.begin(export_csv, 1, 5000);
        CHECK(!again.allow && again.retry_after_ms == 25000);
        CHECK(limiter.begin(history, 1, 5000).allow); // history is its own budget
        limiter.finish(history);
        CHECK(limiter.begin(export_csv, 1, 30000).allow);
        limiter.finish(export_csv);
    }
    {
        // At most two history-type requests at once, from any clients; finishing frees a slot.
        PublicLimiter limiter;
        CHECK(limiter.begin(history, 1, 0).allow);
        CHECK(limiter.begin(history, 2, 0).allow);
        auto third = limiter.begin(history, 3, 0);
        CHECK(!third.allow && third.retry_after_ms >= 1000);
        // A refused request does not use up the client's one-per-second budget.
        limiter.finish(history);
        CHECK(limiter.begin(history, 3, 100).allow);
        limiter.finish(history);
        limiter.finish(history);
        // An export holds a slot while it streams.
        CHECK(limiter.begin(export_csv, 4, 100000).allow);
        CHECK(limiter.begin(history, 5, 100000).allow);
        CHECK(!limiter.begin(history, 6, 100000).allow);
    }
    {
        // More clients than table entries: the least recently used one is forgotten, never a
        // client that is currently limited out of existence for another.
        PublicLimiter limiter;
        for (uint32_t client = 1; client <= 12; ++client) {
            CHECK(limiter.begin(history, client, int64_t(client) * 10).allow);
            limiter.finish(history);
        }
        // The newest client is still remembered and limited.
        CHECK(!limiter.begin(history, 12, 125).allow);
    }
    {
        // A clock that goes backwards must not lock a client out for a long time.
        PublicLimiter limiter;
        CHECK(limiter.begin(history, 1, 1000000).allow);
        limiter.finish(history);
        auto later = limiter.begin(history, 1, 5);
        CHECK(later.allow || later.retry_after_ms <= 1000);
    }
    std::cout << "PASS: public history and export rate limits\n";
}
