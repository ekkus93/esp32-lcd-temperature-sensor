#pragma once
#include "esp_err.h"
#include "sessions.hpp"
#include <array>
#include <cstdint>
#include <string>
namespace monitor {
std::string secure_token(size_t bytes = 24);
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
    Session *login(const std::string &password, int64_t now_ms);
    Session *authorize(const std::string &token, int64_t now_ms);
    void logout(const std::string &token);
    bool change_password(const std::string &token, const std::string &old_password,
                         const std::string &new_password, int64_t now_ms);

  private:
    bool derive(const std::string &password, const std::array<uint8_t, 16> &salt,
                std::array<uint8_t, 32> &hash, uint32_t iterations = 100000);
    bool save_password(const std::string &password);
    bool check_password(const std::string &password);
    bool enrolled_ = false;
    bool ready_ = false;
    std::string setup_;
    std::array<uint8_t, 16> salt_{};
    std::array<uint8_t, 32> hash_{};
    SessionStore sessions_;
    int64_t next_login_ = 0;
    unsigned failures_ = 0;
};
} // namespace monitor
