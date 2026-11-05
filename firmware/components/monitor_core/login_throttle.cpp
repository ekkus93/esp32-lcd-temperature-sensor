#include "login_throttle.hpp"
#include <algorithm>

namespace monitor {
namespace {
constexpr int64_t busy_retry_ms = 1000;
constexpr int64_t success_spacing_ms = 500;
constexpr int64_t kdf_error_retry_ms = 2000;
constexpr int64_t first_backoff_ms = 5000;
constexpr int64_t max_backoff_ms = 60000;
} // namespace

LoginThrottle::Client *LoginThrottle::find(uint32_t client) {
    for (auto &entry : clients_)
        if (entry.used && entry.id == client)
            return &entry;
    return nullptr;
}

LoginThrottle::Client *LoginThrottle::claim(uint32_t client, int64_t now_ms) {
    if (auto *existing = find(client)) {
        existing->last_used_ms = now_ms;
        return existing;
    }
    // Reuse a free slot, otherwise the least recently used one.
    Client *slot = nullptr;
    for (auto &entry : clients_)
        if (!entry.used) {
            slot = &entry;
            break;
        }
    if (!slot)
        slot = &*std::min_element(
            clients_.begin(), clients_.end(),
            [](const Client &a, const Client &b) { return a.last_used_ms < b.last_used_ms; });
    *slot = Client{client, true, 0, 0, now_ms};
    return slot;
}

LoginThrottle::Decision LoginThrottle::begin(uint32_t client, int64_t now_ms) {
    if (in_flight_)
        return {LoginGate::busy, busy_retry_ms};
    auto *entry = claim(client, now_ms);
    if (entry->next_ms > now_ms)
        return {LoginGate::locked, entry->next_ms - now_ms};
    in_flight_ = true;
    return {LoginGate::allow, 0};
}

void LoginThrottle::finish(uint32_t client, LoginOutcome outcome, int64_t now_ms) {
    in_flight_ = false;
    auto *entry = claim(client, now_ms);
    switch (outcome) {
    case LoginOutcome::verified:
        entry->failures = 0;
        entry->next_ms = now_ms + success_spacing_ms;
        break;
    case LoginOutcome::wrong_password: {
        const unsigned step = std::min(entry->failures, 4u);
        entry->failures = entry->failures + 1;
        entry->next_ms = now_ms + std::min(max_backoff_ms, first_backoff_ms << step);
        break;
    }
    case LoginOutcome::kdf_error:
        // The device failed, not the client: no escalation, just a short pause.
        entry->next_ms = now_ms + kdf_error_retry_ms;
        break;
    }
}

void LoginThrottle::abort(uint32_t) {
    in_flight_ = false;
}
} // namespace monitor
