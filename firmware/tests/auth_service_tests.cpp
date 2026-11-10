// Runs the real AuthService (enrollment, login, throttle, sessions, password change) on the host.
// FreeRTOS, NVS and PSA are stood in for by tests/auth_mocks; the password derivation is a real
// PBKDF2-HMAC-SHA256, so the on-device known-answer self-test also runs.
#include "auth_service.hpp"
#include "nvs.h"
#include "psa/crypto.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

int64_t esp_timer_get_time() {
    return 0; // the elapsed-time arithmetic in login_complete sees zero cost
}

namespace {
const std::string good_password = "a-long-example-password";
constexpr int64_t T0 = 1000000;

// An initialised, not yet enrolled service.
struct Fresh {
    AuthService auth;
    Fresh() {
        FakeNvs::reset();
        psa_mock_fail = false;
        CHECK(auth.initialize() == ESP_OK);
    }
};
void enroll_admin(AuthService &auth, int64_t now = T0) {
    CHECK(auth.enroll(good_password, now) == AuthService::EnrollResult::enrolled);
}
} // namespace

int main() {
    // A first start is not enrolled and creates no owner code or verifier.
    {
        Fresh f;
        CHECK(!f.auth.enrolled());
        CHECK(!FakeNvs::has("verifier1"));
        CHECK(!FakeNvs::has("bootstrap"));
    }
    // Enrollment: a short password is refused, the 5 second spacing applies, and once enrolled
    // it cannot be done again.
    {
        Fresh f;
        auto &a = f.auth;
        CHECK(a.enroll("short", T0) == AuthService::EnrollResult::denied);
        CHECK(!a.enrolled() && !FakeNvs::has("verifier1"));
        CHECK(a.enroll(good_password, T0 + 1000) == AuthService::EnrollResult::denied);
        CHECK(!a.enrolled());
        CHECK(a.enroll(good_password, T0 + 6000) == AuthService::EnrollResult::enrolled);
        CHECK(a.enrolled() && FakeNvs::has("verifier1"));
        CHECK(a.enroll("another-long-password", T0 + 40000) == AuthService::EnrollResult::denied);
    }
    // A storage failure while enrolling leaves the device unenrolled; a retry later works.
    {
        Fresh f;
        FakeNvs::fail_commit = true;
        CHECK(f.auth.enroll(good_password, T0) == AuthService::EnrollResult::denied);
        CHECK(!f.auth.enrolled());
        FakeNvs::fail_commit = false;
        CHECK(f.auth.enroll(good_password, T0 + 10000) == AuthService::EnrollResult::enrolled);
    }
    // Login: right and wrong passwords, per-client backoff, one derive at a time, abort.
    {
        Fresh f;
        auto &a = f.auth;
        enroll_admin(a);
        auto begin = a.login_begin(1, T0);
        CHECK(begin.gate == LoginGate::allow);
        // A second client has to wait for the derive in flight.
        CHECK(a.login_begin(2, T0).gate == LoginGate::busy);
        const auto session = a.login_complete(good_password, 1, T0);
        CHECK(session && !session->token.empty() && !session->csrf.empty());
        CHECK(a.authorize(session->token, T0 + 1000));
        CHECK(!a.authorize("not-a-token", T0 + 1000));
        // A wrong password is refused and the same client has to back off.
        CHECK(a.login_begin(1, T0 + 5000).gate == LoginGate::allow);
        CHECK(!a.login_complete("wrong-password-here", 1, T0 + 5000));
        const auto locked = a.login_begin(1, T0 + 5100);
        CHECK(locked.gate == LoginGate::locked && locked.retry_after_ms > 0);
        // Another address is not affected by it.
        CHECK(a.login_begin(2, T0 + 5100).gate == LoginGate::allow);
        a.login_abort(2); // it never reached the derive
        CHECK(a.login_begin(3, T0 + 5100).gate == LoginGate::allow);
        a.login_abort(3);
        // Logout ends the session.
        a.logout(session->token);
        CHECK(!a.authorize(session->token, T0 + 6000));
    }
    // A broken crypto engine is a device failure, not a wrong password: a short pause, no strike.
    {
        Fresh f;
        auto &a = f.auth;
        enroll_admin(a);
        CHECK(a.login_begin(1, T0).gate == LoginGate::allow);
        psa_mock_fail = true;
        CHECK(!a.login_complete(good_password, 1, T0));
        psa_mock_fail = false;
        const auto pause = a.login_begin(1, T0 + 100);
        CHECK(pause.gate == LoginGate::locked && pause.retry_after_ms <= 2000);
        CHECK(a.login_begin(1, T0 + 2500).gate == LoginGate::allow);
        CHECK(a.login_complete(good_password, 1,
                               T0 + 2500)); // the password was never held against it
    }
    // A password change: needs the session and the old password, ends all sessions, and the new
    // password is the only one that works afterwards.
    {
        Fresh f;
        auto &a = f.auth;
        enroll_admin(a);
        CHECK(a.login_begin(1, T0).gate == LoginGate::allow);
        const auto session = a.login_complete(good_password, 1, T0);
        CHECK(session);
        const std::string better = "an-even-better-password";
        CHECK(!a.change_password("bad-token", good_password, better, T0 + 5000));
        CHECK(!a.change_password(session->token, "not-the-old-password", better, T0 + 5000));
        // The refusal sets a spacing before the next attempt.
        CHECK(!a.change_password(session->token, good_password, better, T0 + 6000));
        CHECK(!a.change_password(session->token, good_password, "short", T0 + 20000));
        // A refused change (here: a too-short new password) also sets the spacing.
        CHECK(!a.change_password(session->token, good_password, better, T0 + 21000));
        CHECK(a.change_password(session->token, good_password, better, T0 + 30000));
        CHECK(!a.authorize(session->token, T0 + 40000)); // the old session ended
        CHECK(a.login_begin(2, T0 + 40000).gate == LoginGate::allow);
        CHECK(!a.login_complete(good_password, 2, T0 + 40000));
        CHECK(a.login_begin(3, T0 + 40000).gate == LoginGate::allow);
        CHECK(a.login_complete(better, 3, T0 + 40000));
    }
    // After a restart the stored verifier is used: the same password works, enrollment is over.
    {
        Fresh f;
        enroll_admin(f.auth);
        AuthService restarted;
        CHECK(restarted.initialize() == ESP_OK);
        CHECK(restarted.enrolled());
        CHECK(restarted.login_begin(1, T0).gate == LoginGate::allow);
        CHECK(restarted.login_complete(good_password, 1, T0));
        CHECK(restarted.enroll(good_password, T0 + 9000) == AuthService::EnrollResult::denied);
    }
    // A leftover owner code from an older firmware is ignored: it does not block or change setup.
    {
        FakeNvs::reset();
        FakeNvs::put("bootstrap", "ABCDEFGHJKLMNPQR");
        AuthService old_firmware;
        CHECK(old_firmware.initialize() == ESP_OK);
        CHECK(!old_firmware.enrolled());
        CHECK(old_firmware.enroll(good_password, T0) == AuthService::EnrollResult::enrolled);
    }
    // Storage that cannot be opened stops startup; the service never becomes usable.
    {
        FakeNvs::reset();
        FakeNvs::fail_open = true;
        AuthService broken;
        CHECK(broken.initialize() != ESP_OK);
        CHECK(broken.enroll(good_password, T0) == AuthService::EnrollResult::denied);
        FakeNvs::fail_open = false;
    }
    // A broken crypto engine at start is caught by the known-answer self-test.
    {
        FakeNvs::reset();
        psa_mock_fail = true;
        AuthService broken;
        CHECK(broken.initialize() != ESP_OK);
        psa_mock_fail = false;
    }
    // Other tasks read the enrolled flag while the HTTP task enrolls: nothing is lost or
    // deadlocked.
    {
        Fresh f;
        std::atomic<bool> stop{false};
        std::thread reader([&] {
            while (!stop)
                (void)f.auth.enrolled();
        });
        enroll_admin(f.auth);
        stop = true;
        reader.join();
        CHECK(f.auth.enrolled());
    }
    // Logins from two clients at once: one derive runs at a time and both finish.
    {
        Fresh f;
        enroll_admin(f.auth);
        CHECK(f.auth.login_begin(1, T0).gate == LoginGate::allow);
        std::optional<Session> first, second;
        std::thread one([&] { first = f.auth.login_complete(good_password, 1, T0); });
        one.join();
        CHECK(first);
        CHECK(f.auth.login_begin(2, T0 + 600).gate == LoginGate::allow);
        std::thread two([&] { second = f.auth.login_complete(good_password, 2, T0 + 600); });
        two.join();
        CHECK(second && second->token != first->token);
    }
    std::cout << "PASS: AuthService enrollment, login, throttle, sessions, password change, "
                 "restart and cross-task reads\n";
}
