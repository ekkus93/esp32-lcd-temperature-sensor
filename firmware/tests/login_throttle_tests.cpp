#include "login_throttle.hpp"
#include <array>
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
    const uint32_t owner = 0x0a000002, attacker = 0x0a000063;

    // One derive runs at a time: a second login while one is in flight is refused at once.
    {
        LoginThrottle throttle;
        CHECK(throttle.begin(owner, 0).gate == LoginGate::allow);
        const auto busy = throttle.begin(attacker, 100);
        CHECK(busy.gate == LoginGate::busy && busy.retry_after_ms > 0);
        throttle.finish(owner, LoginOutcome::verified, 15000);
        CHECK(throttle.begin(attacker, 15000).gate == LoginGate::allow);
    }

    // A client's wrong passwords back it off (5, 10, 20, 40, 60 s) without touching others.
    {
        LoginThrottle throttle;
        int64_t now = 0;
        const int64_t expected[] = {5000, 10000, 20000, 40000, 60000, 60000};
        for (int64_t wait : expected) {
            CHECK(throttle.begin(attacker, now).gate == LoginGate::allow);
            throttle.finish(attacker, LoginOutcome::wrong_password, now + 15000);
            now += 15000;
            const auto locked = throttle.begin(attacker, now);
            CHECK(locked.gate == LoginGate::locked);
            CHECK(locked.retry_after_ms == wait);
            // The owner on another address is not locked out by the attacker's failures.
            CHECK(throttle.begin(owner, now).gate == LoginGate::allow);
            throttle.finish(owner, LoginOutcome::verified, now + 15000);
            now += 15000;
            now += wait; // sit out the lockout
        }
    }

    // A correct password clears the client's failures and only imposes a short spacing.
    {
        LoginThrottle throttle;
        CHECK(throttle.begin(owner, 0).gate == LoginGate::allow);
        throttle.finish(owner, LoginOutcome::wrong_password, 15000);
        CHECK(throttle.begin(owner, 15000).gate == LoginGate::locked);
        CHECK(throttle.begin(owner, 20000).gate == LoginGate::allow);
        throttle.finish(owner, LoginOutcome::verified, 35000);
        CHECK(throttle.begin(owner, 35000).gate == LoginGate::locked);
        CHECK(throttle.begin(owner, 35500).gate == LoginGate::allow);
        throttle.finish(owner, LoginOutcome::wrong_password, 50500);
        // Back to the first backoff step, not a continuation of the earlier failure.
        const auto locked = throttle.begin(owner, 50500);
        CHECK(locked.gate == LoginGate::locked && locked.retry_after_ms == 5000);
    }

    // A failure of the key derivation itself is not a wrong password: no escalating backoff,
    // only a short retry delay, and the in-flight slot is released.
    {
        LoginThrottle throttle;
        for (int attempt = 0; attempt < 6; ++attempt) {
            const int64_t now = attempt * 10000;
            CHECK(throttle.begin(owner, now).gate == LoginGate::allow);
            throttle.finish(owner, LoginOutcome::kdf_error, now + 100);
            const auto locked = throttle.begin(owner, now + 100);
            CHECK(locked.gate == LoginGate::locked && locked.retry_after_ms <= 2000);
        }
        CHECK(throttle.begin(owner, 100000).gate == LoginGate::allow);
        throttle.finish(owner, LoginOutcome::verified, 100100);
    }

    // Many addresses: the client table is bounded and the in-flight rule still holds.
    {
        LoginThrottle throttle;
        int64_t now = 0;
        for (uint32_t client = 1; client <= 40; ++client, now += 1000) {
            CHECK(throttle.begin(client, now).gate == LoginGate::allow);
            CHECK(throttle.begin(client + 1000, now).gate == LoginGate::busy);
            throttle.finish(client, LoginOutcome::wrong_password, now + 500);
        }
        CHECK(throttle.begin(owner, now).gate == LoginGate::allow);
    }

    // Aborting a request that never reached the derive (queue full) releases the slot.
    {
        LoginThrottle throttle;
        CHECK(throttle.begin(owner, 0).gate == LoginGate::allow);
        throttle.abort(owner);
        CHECK(throttle.begin(owner, 0).gate == LoginGate::allow);
    }

    // A client is identified by its address only, never by the source port, and an IPv4 peer
    // reported as an IPv4-mapped IPv6 address is the same client as the plain IPv4 address.
    {
        const uint32_t v4 = 0x4140400a; // as read from sockaddr_in.sin_addr (network byte order)
        std::array<uint8_t, 16> mapped{};
        mapped[10] = 0xff;
        mapped[11] = 0xff;
        mapped[12] = 0x0a;
        mapped[13] = 0x40;
        mapped[14] = 0x40;
        mapped[15] = 0x41;
        CHECK(client_identity_ipv6(mapped.data()) == v4);
        std::array<uint8_t, 16> global_a{0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                                         0,    0,    0,    0,    0, 0, 0, 1};
        std::array<uint8_t, 16> global_b = global_a;
        global_b[15] = 2;
        CHECK(client_identity_ipv6(global_a.data()) == client_identity_ipv6(global_a.data()));
        CHECK(client_identity_ipv6(global_a.data()) != client_identity_ipv6(global_b.data()));
        CHECK(client_identity_ipv6(global_a.data()) != 0);
        std::array<uint8_t, 16> v4_other = mapped;
        v4_other[15] = 0x42;
        CHECK(client_identity_ipv6(v4_other.data()) != client_identity_ipv6(mapped.data()));
    }
    std::cout << "PASS: login throttle is per client, single-flight and ignores derive failures\n";
}
