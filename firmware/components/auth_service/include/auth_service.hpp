#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "login_throttle.hpp"
#include "sessions.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
namespace monitor {
std::string secure_token(size_t bytes = 24);
// Random code from the unambiguous typing alphabet; empty on failure. See readable_code.hpp.
std::string readable_secret(size_t length);
class AuthService {
  public:
    esp_err_t initialize(); // Call after radio start so hardware entropy is available.
    bool enrolled() const {
        return enrolled_;
    }
    const std::string &setup_secret() const {
        return setup_;
    }
    bool enroll(const std::string &secret, const std::string &password, int64_t now_ms);
    // Login is split so the slow password check can run on a worker task instead of the HTTP
    // task. login_begin() asks the throttle (per client, one derive at a time); on `allow` the
    // caller must follow with login_complete() or login_abort().
    LoginThrottle::Decision login_begin(uint32_t client, int64_t now_ms);
    void login_abort(uint32_t client);
    // Runs the password check (about 15 s); never call it from the HTTP task.
    std::optional<Session> login_complete(const std::string &password, uint32_t client,
                                          int64_t now_ms);
    // Returns a copy, so the caller never holds a pointer into the shared session table.
    std::optional<Session> authorize(const std::string &token, int64_t now_ms);
    void logout(const std::string &token);
    bool change_password(const std::string &token, const std::string &old_password,
                         const std::string &new_password, int64_t now_ms);

  private:
    bool derive(const std::string &password, const std::array<uint8_t, 16> &salt,
                std::array<uint8_t, 32> &hash, uint32_t iterations = 100000);
    bool save_password(const std::string &password);
    enum class PasswordCheck { match, mismatch, kdf_failed };
    PasswordCheck check_password_result(const std::string &password);
    bool check_password(const std::string &password) {
        return check_password_result(password) == PasswordCheck::match;
    }
    bool enrolled_ = false;
    bool ready_ = false;
    std::string setup_;
    std::array<uint8_t, 16> salt_{};
    std::array<uint8_t, 32> hash_{};
    SessionStore sessions_;
    LoginThrottle throttle_;
    int64_t next_login_ = 0; // spacing for enrollment and password changes
    // mutex_ guards sessions_ and throttle_ for short operations only. kdf_mutex_ serialises the
    // memory-heavy key derivations across tasks.
    SemaphoreHandle_t mutex_ = nullptr;
    SemaphoreHandle_t kdf_mutex_ = nullptr;
};
} // namespace monitor
