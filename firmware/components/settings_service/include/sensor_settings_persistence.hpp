#pragma once
#include "settings_service.hpp"
#include <cstdint>
namespace monitor {
// The main loop's decisions about writing sensor settings and taking edits (review audit F3).
//
// Damaged or unsupported stored settings found at load stay read-only for the whole boot, with the
// data preserved. A failed write (open, set or commit) is transient: the edit is rolled back, and
// edits are refused for a backoff of 30 seconds, doubling up to 10 minutes, then accepted again.
// The pause is what tells the browser the edit was not committed (the storage status reads
// `read_only` while it lasts; schema v1 has no other field for it), and it keeps automatic saves
// from retrying a failing store every sampling cycle. A successful write clears the backoff.
class SensorSettingsPersistence {
  public:
    static constexpr int64_t first_backoff_ms = 30 * 1000;
    static constexpr int64_t max_backoff_ms = 10 * 60 * 1000;

    explicit SensorSettingsPersistence(SettingsService &service) : service_(service) {}
    // Commits owner edits already applied to `registry`. On failure `registry` is restored to
    // `committed` (with its revisions) and edits pause for the backoff.
    esp_err_t commit_edits(Registry &registry, const Registry &committed, int64_t now_ms);
    // Persists automatic changes (a probe discovered, a probe's own calibration) once due.
    // `changed` marks this cycle's change; a change not yet written is retried after the backoff.
    // Returns ESP_OK when nothing was due or the write succeeded, ESP_ERR_NOT_FINISHED while
    // waiting.
    esp_err_t persist(const Registry &registry, bool changed, int64_t now_ms);
    // Whether the API may accept sensor edits now (advertised as the storage status).
    bool accepting_edits(int64_t now_ms) const {
        return service_.writable() && now_ms >= retry_at_ms_;
    }
    // A change that is applied in RAM but not yet stored.
    bool unsaved() const {
        return unsaved_;
    }
    int64_t retry_at_ms() const {
        return retry_at_ms_;
    }

  private:
    esp_err_t write(const Registry &registry, int64_t now_ms);
    SettingsService &service_;
    bool unsaved_ = false;
    int64_t retry_at_ms_ = INT64_MIN;
    int64_t backoff_ms_ = 0;
};
} // namespace monitor
