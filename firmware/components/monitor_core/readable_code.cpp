#include "readable_code.hpp"
#include <array>
#include <cstring>

namespace monitor {
std::string make_readable_code(size_t length, const RandomFill &fill) {
    if (length == 0 || length > 64 || !fill)
        return {};
    std::array<uint8_t, 64> bytes{};
    if (!fill(bytes.data(), length))
        return {};
    std::string code(length, '\0');
    for (size_t i = 0; i < length; ++i)
        code[i] = readable_code_alphabet[bytes[i] & 31];
    return code;
}

bool is_readable_code(const std::string &code, size_t length) {
    if (code.size() != length)
        return false;
    for (char c : code)
        if (c == '\0' || !std::strchr(readable_code_alphabet, c))
            return false;
    return true;
}

SetupCodeAction plan_setup_code(bool stored, const std::string &value) {
    if (!stored)
        return SetupCodeAction::create;
    if (is_readable_code(value, owner_setup_code_length))
        return SetupCodeAction::keep;
    if (is_legacy_hex_secret(value, 64))
        return SetupCodeAction::rotate;
    return SetupCodeAction::create;
}

bool is_legacy_hex_secret(const std::string &secret, size_t length) {
    if (secret.size() != length)
        return false;
    for (char c : secret)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}
} // namespace monitor
