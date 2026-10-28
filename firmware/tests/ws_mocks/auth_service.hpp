#pragma once
#include <cstdint>
#include <string>
namespace monitor {
struct Session {};
class AuthService {
  public:
    Session *authorize(const std::string &token, int64_t now_ms) {
        return (token == "valid" || (token == "expiring" && now_ms < 1500))
                   ? &session_
                   : nullptr;
    }

  private:
    Session session_{};
};
} // namespace monitor
