#include "public_limiter.hpp"

namespace monitor {
PublicLimiter::Decision PublicLimiter::begin(PublicRequest kind, uint32_t client, int64_t now_ms) {
    Client *entry = nullptr;
    for (auto &c : clients_)
        if (c.used && c.id == client)
            entry = &c;
    if (!entry) {
        // Free slot first, otherwise the least recently used client is forgotten.
        for (auto &c : clients_)
            if (!c.used) {
                entry = &c;
                break;
            }
        if (!entry) {
            entry = &clients_[0];
            for (auto &c : clients_)
                if (c.last_used_ms < entry->last_used_ms)
                    entry = &c;
        }
        *entry = Client{};
        entry->used = true;
        entry->id = client;
    }
    const bool exporting = kind == PublicRequest::export_csv;
    const int64_t interval = exporting ? export_interval_ms : history_interval_ms;
    const bool had = exporting ? entry->had_export : entry->had_history;
    const int64_t last = exporting ? entry->last_export_ms : entry->last_history_ms;
    entry->last_used_ms = now_ms;
    if (had) {
        // A clock that moved backwards counts as a fresh start rather than a long lockout.
        const int64_t elapsed = now_ms >= last ? now_ms - last : interval;
        if (elapsed < interval)
            return {false, interval - elapsed};
    }
    if (running_ >= max_concurrent)
        return {false, history_interval_ms};
    ++running_;
    if (exporting) {
        entry->had_export = true;
        entry->last_export_ms = now_ms;
    } else {
        entry->had_history = true;
        entry->last_history_ms = now_ms;
    }
    return {true, 0};
}

void PublicLimiter::finish(PublicRequest) {
    if (running_)
        --running_;
}
} // namespace monitor
