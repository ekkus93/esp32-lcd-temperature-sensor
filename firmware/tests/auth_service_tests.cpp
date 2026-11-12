// Runs the real AuthService (enrollment, login, throttle, sessions, password change) on the host.
// FreeRTOS, NVS and PSA are stood in for by tests/auth_mocks; the password derivation is a real
// PBKDF2-HMAC-SHA256, so the on-device known-answer self-test also runs.
#include "auth_service.hpp"
#include "esp_heap_caps.h"
#include "freertos/task.h"
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

// A password saved by firmware before the round count was stored: PBKDF2-HMAC-SHA256 of
// good_password, salt 1..16, 100,000 rounds, laid out as salt then hash.
const std::string legacy_record("\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10"
                                "\x9b\x3f\x2a\xbd\xb4\x5f\x94\xa7\xf4\x3c\xae\x3e\xcd\x74\x56\x0e"
                                "\x87\x70\x48\x0f\xd4\x0c\x65\x52\x31\x42\xc5\xe5\x16\x37\x5d\x4f",
                                48);

int main() {
    // A first start is not enrolled and creates no owner code or verifier.
    {
        Fresh f;
        CHECK(!f.auth.enrolled());
        CHECK(!FakeNvs::has("verifier2"));
        CHECK(!FakeNvs::has("bootstrap"));
    }
    // Enrollment: a short password is refused, the 5 second spacing applies, and once enrolled
    // it cannot be done again.
    {
        Fresh f;
        auto &a = f.auth;
        CHECK(a.enroll("short", T0) == AuthService::EnrollResult::denied);
        CHECK(!a.enrolled() && !FakeNvs::has("verifier2"));
        CHECK(a.enroll(good_password, T0 + 1000) == AuthService::EnrollResult::denied);
        CHECK(!a.enrolled());
        CHECK(a.enroll(good_password, T0 + 6000) == AuthService::EnrollResult::enrolled);
        CHECK(a.enrolled() && FakeNvs::has("verifier2"));
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
    // The login says why it failed. When the password worker cannot be started (no memory for its
    // stack) that is the device's failure: reported as unavailable, with no strike against the
    // client. The worker's stack is asked for in PSRAM, not in the scarce internal RAM.
    {
        Fresh f;
        auto &a = f.auth;
        enroll_admin(a);
        CHECK((FakeTasks::last_caps & MALLOC_CAP_SPIRAM) != 0);
        auto failure = AuthService::LoginFailure::none;
        CHECK(a.login_begin(1, T0).gate == LoginGate::allow);
        CHECK(!a.login_complete("wrong-password-here", 1, T0, &failure));
        CHECK(failure == AuthService::LoginFailure::wrong_password);
        FakeTasks::fail_create = true;
        CHECK(a.login_begin(2, T0).gate == LoginGate::allow);
        CHECK(!a.login_complete(good_password, 2, T0, &failure));
        CHECK(failure == AuthService::LoginFailure::unavailable);
        FakeTasks::fail_create = false;
        CHECK(a.login_begin(2, T0 + 2500).gate == LoginGate::allow);
        CHECK(a.login_complete(good_password, 2, T0 + 2500, &failure));
        CHECK(failure == AuthService::LoginFailure::none);
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
    // A password saved at the old round count still signs in, and is moved to the current count on
    // that login: the legacy record is replaced by a 10,000-round record.
    {
        FakeNvs::reset();
        FakeNvs::put("verifier1", legacy_record);
        AuthService a;
        CHECK(a.initialize() == ESP_OK && a.enrolled());
        CHECK(a.login_begin(1, T0).gate == LoginGate::allow);
        CHECK(!a.login_complete("wrong-password-here", 1, T0)); // a wrong guess does not upgrade
        CHECK(FakeNvs::has("verifier1") && !FakeNvs::has("verifier2"));
        CHECK(a.login_begin(1, T0 + 5000).gate == LoginGate::allow);
        CHECK(a.login_complete(good_password, 1, T0 + 5000));
        CHECK(!FakeNvs::has("verifier1") && FakeNvs::has("verifier2"));
        const auto upgraded = FakeNvs::get("verifier2");
        CHECK(upgraded.size() == 52);
        CHECK(uint8_t(upgraded[0]) == 0x10 &&
              uint8_t(upgraded[1]) == 0x27); // 10,000, little-endian
        AuthService restarted;
        CHECK(restarted.initialize() == ESP_OK && restarted.enrolled());
        CHECK(restarted.login_begin(2, T0 + 20000).gate == LoginGate::allow);
        CHECK(restarted.login_complete(good_password, 2, T0 + 20000));
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
    // A leftover owner code from older firmware is erased at startup, and the stored password is
    // left exactly as it was. A device that is not enrolled yet loses the code as well.
    {
        Fresh f;
        enroll_admin(f.auth);
        const auto verifier = FakeNvs::get("verifier2");
        FakeNvs::put("bootstrap", "ABCDEFGHJKLMNPQR");
        AuthService restarted;
        CHECK(restarted.initialize() == ESP_OK && restarted.enrolled());
        CHECK(!FakeNvs::has("bootstrap"));
        CHECK(FakeNvs::get("verifier2") == verifier);
        FakeNvs::reset();
        FakeNvs::put("bootstrap", "ABCDEFGHJKLMNPQR");
        AuthService unenrolled;
        CHECK(unenrolled.initialize() == ESP_OK && !unenrolled.enrolled());
        CHECK(!FakeNvs::has("bootstrap"));
        AuthService again; // nothing left to erase is fine
        CHECK(again.initialize() == ESP_OK);
    }
    // USB recovery removes the password whichever record holds it, and leaves the device ready to
    // enroll again.
    {
        Fresh f;
        enroll_admin(f.auth);
        FakeNvs::put("verifier1", legacy_record);
        FakeNvs::put("other", "kept");
        CHECK(AuthService::erase_stored_password());
        CHECK(!FakeNvs::has("verifier2") && !FakeNvs::has("verifier1"));
        CHECK(FakeNvs::get("other") == "kept");
        AuthService restarted;
        CHECK(restarted.initialize() == ESP_OK && !restarted.enrolled());
        CHECK(AuthService::erase_stored_password()); // nothing stored is still a success
        FakeNvs::fail_open = true;
        CHECK(!AuthService::erase_stored_password());
        FakeNvs::fail_open = false;
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
