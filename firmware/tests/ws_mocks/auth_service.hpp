#pragma once
#include <cstdint>
#include <string>
namespace monitor {
struct Session {};
class AuthService {
  public:
    Session *authorize(const std::string &token, int64_t now_ms) {
        if (token == "valid" || (token == "expiring" && now_ms < 1500))
            return &session_;
        return nullptr;
    }

  private:
    Session session_{};
};
} // namespace monitor
