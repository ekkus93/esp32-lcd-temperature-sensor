#include "sessions.hpp"
#include <algorithm>
namespace monitor {
bool constant_equal(const std::string &left, const std::string &right) {
    if (left.size() != right.size())
        return false;
    volatile unsigned diff = 0;
    for (size_t i = 0; i < left.size(); ++i)
        diff = diff | static_cast<unsigned char>(left[i] ^ right[i]);
    return diff == 0;
}
Session *SessionStore::create(const std::string &token, const std::string &csrf, int64_t now_ms) {
    if (token.size() != 64 || csrf.size() != 48 || now_ms < 0)
        return nullptr;
    auto *slot = &sessions_[0];
    for (auto &session : sessions_)
        if (session.expires_ms < slot->expires_ms)
            slot = &session;
    *slot = {token, csrf, now_ms + 3600000};
    return slot;
}
Session *SessionStore::authorize(const std::string &token, int64_t now_ms) {
    if (token.size() != 64 || now_ms < 0)
        return nullptr;
    for (auto &session : sessions_)
        if (session.expires_ms > now_ms && constant_equal(token, session.token))
            return &session;
    return nullptr;
}
void SessionStore::logout(const std::string &token) {
    const auto match = token;
    for (auto &session : sessions_)
        if (constant_equal(session.token, match))
            session = {};
}
void SessionStore::clear() {
    for (auto &session : sessions_)
        session = {};
}
} // namespace monitor
