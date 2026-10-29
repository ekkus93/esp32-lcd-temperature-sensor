#pragma once
#include "monitor.hpp"
#include <functional>
namespace monitor {
constexpr size_t max_history_record_bytes = 335;
// All integer fields are encoded little-endian; no native struct padding is persisted.
std::vector<uint8_t> encode_bucket(const Bucket &bucket);
std::optional<Bucket> decode_bucket(const std::vector<uint8_t> &bytes);
class HistoryStore {
  public:
    explicit HistoryStore(std::string directory, std::size_t max_bytes = 3 * 1024 * 1024,
                          std::size_t segment_bytes = 32 * 1024);
    bool recover();
    bool append(const Bucket &bucket);
    bool visit(const std::function<bool(const Bucket &)> &visitor) const;
    std::size_t bytes() const;

  private:
    std::string directory_;
    std::size_t max_bytes_;
    std::size_t segment_bytes_;
    unsigned active_ = 0;
    // No writes before first successful recovery or after a failed append/recovery.
    bool append_ready_ = false;
    // Detect deletion of the active segment after a successful recovery/write.
    bool active_segment_exists_ = false;
#ifndef ESP_PLATFORM
    // Directory identity observed at the most recent successful recovery.
    uint64_t directory_dev_ = 0;
    uint64_t directory_ino_ = 0;
#endif
    std::vector<unsigned> segments() const;
    std::string path(unsigned number) const;
};
} // namespace monitor
