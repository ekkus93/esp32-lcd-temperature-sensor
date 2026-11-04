#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace monitor {
// Setup secrets that people type from the LCD use 32 unambiguous characters (no 0, 1, I or O),
// so one random byte maps to a character with no modulo bias and each character is 5 bits.
inline constexpr char readable_code_alphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
inline constexpr size_t wifi_setup_key_length = 12;   // 60 bits
inline constexpr size_t owner_setup_code_length = 16; // 80 bits

using RandomFill = std::function<bool(uint8_t *bytes, size_t size)>;

// Returns an empty string if the length is unusable or the random source fails.
std::string make_readable_code(size_t length, const RandomFill &fill);
bool is_readable_code(const std::string &code, size_t length);
// The previous format: lowercase hex of the given number of characters.
bool is_legacy_hex_secret(const std::string &secret, size_t length);
} // namespace monitor
