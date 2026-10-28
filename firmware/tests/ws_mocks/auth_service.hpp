#pragma once
#include <cstdint>
#include <string>
namespace monitor {
struct Session {};
class AuthService {
  public:
    Session *authorize(const std::string &token, int64_t) {
        return token == "valid" ? &session_ : nullptr;
    }

  private:
    Session session_{};
};
} // namespace monitor
