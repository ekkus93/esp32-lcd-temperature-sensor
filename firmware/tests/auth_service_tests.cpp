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

std::string lower(std::string text) {
    for (auto &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
// An initialised, not yet enrolled service with a fresh owner code.
struct Fresh {
    AuthService auth;
    std::string code;
    Fresh() {
        FakeNvs::reset();
        psa_mock_fail = false;
        CHECK(auth.initialize() == ESP_OK);
        code = auth.setup_secret();
    }
};
void enroll_owner(AuthService &auth, const std::string &code, int64_t now = T0) {
    CHECK(auth.enroll(code, good_password, now) == AuthService::EnrollResult::enrolled);
}
} // namespace

int main() {
    // A first start creates a one-time owner code in the current format and stores it.
    {
        Fresh f;
        CHECK(!f.auth.enrolled());
        CHECK(f.code.size() == 16);
        CHECK(FakeNvs::get("bootstrap") == f.code);
        CHECK(!FakeNvs::has("verifier1"));
    }
    // Enrollment: wrong code, the 5 second spacing, format errors that are not guesses, and a
    // password that is too short.
    {
        Fresh f;
        auto &a = f.auth;
        CHECK(a.enroll("ABCDEFGHJKLMNPQR", good_password, T0) == AuthService::EnrollResult::denied);
        // Even the right code is refused during the spacing after a guess.
        CHECK(a.enroll(f.code, good_password, T0 + 1000) == AuthService::EnrollResult::denied);
        CHECK(!a.enrolled());
        // A malformed code is reported as such and is not a guess: no spacing starts.
        CHECK(a.enroll("short", good_password, T0 + 6000) == AuthService::EnrollResult::bad_format);
        CHECK(a.enroll("O0I1O0I1O0I1O0I1", good_password, T0 + 6000) ==
              AuthService::EnrollResult::bad_format);
        // A short password is refused without enrolling, even with the right code.
        CHECK(a.enroll(f.code, "short", T0 + 6000) == AuthService::EnrollResult::denied);
        CHECK(!a.enrolled() && !FakeNvs::has("verifier1"));
        // What was typed is normalised: lower case and a dash in the middle are fine.
        const auto typed = lower(f.code.substr(0, 8)) + "-" + lower(f.code.substr(8));
        CHECK(a.enroll(typed, good_password, T0 + 20000) == AuthService::EnrollResult::enrolled);
        CHECK(a.enrolled());
        // The owner code is gone everywhere once the owner exists.
        CHECK(a.setup_secret().empty());
        CHECK(!FakeNvs::has("bootstrap") && FakeNvs::has("verifier1"));
        // And cannot be used again.
        CHECK(a.enroll(f.code, good_password, T0 + 40000) == AuthService::EnrollResult::denied);
    }
    // A storage failure while enrolling leaves the device unenrolled and the code usable.
    {
        Fresh f;
        FakeNvs::fail_commit = true;
        CHECK(f.auth.enroll(f.code, good_password, T0) == AuthService::EnrollResult::denied);
        CHECK(!f.auth.enrolled());
        FakeNvs::fail_commit = false;
        CHECK(f.auth.enroll(f.code, good_password, T0 + 10000) ==
              AuthService::EnrollResult::enrolled);
    }
    // Login: right and wrong passwords, per-client backoff, one derive at a time, abort.
    {
        Fresh f;
        auto &a = f.auth;
        enroll_owner(a, f.code);
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
        enroll_owner(a, f.code);
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
        enroll_owner(a, f.code);
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
    // After a restart the stored verifier is used: same password, no owner code any more.
    {
        Fresh f;
        enroll_owner(f.auth, f.code);
        AuthService restarted;
        CHECK(restarted.initialize() == ESP_OK);
        CHECK(restarted.enrolled() && restarted.setup_secret().empty());
        CHECK(restarted.login_begin(1, T0).gate == LoginGate::allow);
        CHECK(restarted.login_complete(good_password, 1, T0));
        CHECK(restarted.enroll(f.code, good_password, T0 + 9000) ==
              AuthService::EnrollResult::denied);
    }
    // A damaged or old-format stored owner code is replaced at start, never left to block setup.
    {
        FakeNvs::reset();
        FakeNvs::put("bootstrap", "not-a-valid-code");
        AuthService damaged;
        CHECK(damaged.initialize() == ESP_OK);
        CHECK(damaged.setup_secret().size() == 16 &&
              FakeNvs::get("bootstrap") == damaged.setup_secret());
        FakeNvs::reset();
        const std::string legacy(64, 'a'); // the old 256-bit hex code
        FakeNvs::put("bootstrap", legacy);
        AuthService old_format;
        CHECK(old_format.initialize() == ESP_OK);
        CHECK(old_format.setup_secret().size() == 16); // shortened to the current format
    }
    // Storage that cannot be opened stops startup; the service never becomes usable.
    {
        FakeNvs::reset();
        FakeNvs::fail_open = true;
        AuthService broken;
        CHECK(broken.initialize() != ESP_OK);
        CHECK(broken.enroll("ANYCODEANYCODEAN", good_password, T0) ==
              AuthService::EnrollResult::denied);
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
    // Other tasks read the owner code and the enrolled flag while the HTTP task enrolls: every
    // read is a consistent value (never a half-cleared code), and nothing is lost or deadlocked.
    {
        Fresh f;
        std::atomic<bool> stop{false};
        std::atomic<int> torn{0};
        std::thread reader([&] {
            while (!stop) {
                const auto code = f.auth.setup_secret();
                if (!code.empty() && code != f.code)
                    ++torn;
                (void)f.auth.enrolled();
            }
        });
        enroll_owner(f.auth, f.code);
        stop = true;
        reader.join();
        CHECK(torn == 0 && f.auth.enrolled() && f.auth.setup_secret().empty());
    }
    // Logins from two clients at once: one derive runs at a time and both finish.
    {
        Fresh f;
        enroll_owner(f.auth, f.code);
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
