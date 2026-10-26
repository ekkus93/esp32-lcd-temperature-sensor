#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace monitor {
bool constant_equal(const std::string &left, const std::string &right);
struct Session {
    std::string token;
    std::string csrf;
    int64_t expires_ms = 0;
};
class SessionStore {
  public:
    Session *create(const std::string &token, const std::string &csrf, int64_t now_ms);
    Session *authorize(const std::string &token, int64_t now_ms);
    void logout(const std::string &token);
    void clear();

  private:
    std::array<Session, 4> sessions_{};
};
} // namespace monitor
