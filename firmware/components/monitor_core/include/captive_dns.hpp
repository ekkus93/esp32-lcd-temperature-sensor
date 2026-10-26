#pragma once
#include <cstdint>
#include <optional>
#include <vector>
namespace monitor {
// Single-question captive DNS, max 512 input bytes. A answers use 192.168.4.1,
// TTL zero; AAAA/other types return no data. No recursion, compression parsing,
// arbitrary destinations, or sensitive information is supported.
std::optional<std::vector<uint8_t>> captive_dns_reply(const std::vector<uint8_t> &query);
} // namespace monitor
