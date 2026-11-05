#include "readable_code.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <set>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
int main() {
    // The alphabet is exactly 32 distinct characters and omits the confusable 0, 1, I and O.
    const std::string alphabet = readable_code_alphabet;
    CHECK(alphabet.size() == 32);
    CHECK(std::set<char>(alphabet.begin(), alphabet.end()).size() == 32);
    for (char banned : std::string("01IOilo"))
        CHECK(alphabet.find(banned) == std::string::npos);

    // Every byte value maps to an alphabet character, uniformly (256 / 32 = 8 each).
    std::array<unsigned, 32> hits{};
    for (unsigned byte = 0; byte < 256; ++byte) {
        const auto code = make_readable_code(1, [&](uint8_t *out, size_t) {
            out[0] = static_cast<uint8_t>(byte);
            return true;
        });
        CHECK(code.size() == 1);
        const auto index = alphabet.find(code[0]);
        CHECK(index != std::string::npos);
        ++hits[index];
    }
    for (auto count : hits)
        CHECK(count == 8);

    // Length, alphabet and failure handling.
    uint8_t counter = 0;
    auto counting = [&](uint8_t *out, size_t size) {
        for (size_t i = 0; i < size; ++i)
            out[i] = counter++;
        return true;
    };
    const auto key = make_readable_code(wifi_setup_key_length, counting);
    const auto owner = make_readable_code(owner_setup_code_length, counting);
    CHECK(key.size() == 12 && is_readable_code(key, 12));
    CHECK(owner.size() == 16 && is_readable_code(owner, 16));
    CHECK(key != owner.substr(0, 12));
    CHECK(wifi_setup_key_length >= 8 && wifi_setup_key_length <= 16); // WPA2 needs 8-63
    CHECK(make_readable_code(0, counting).empty());
    CHECK(make_readable_code(65, counting).empty());
    CHECK(make_readable_code(12, [](uint8_t *, size_t) { return false; }).empty());
    CHECK(make_readable_code(12, nullptr).empty());
    CHECK(!is_readable_code("short", 12));
    CHECK(!is_readable_code("ABCDEFGHJKL0", 12));
    CHECK(!is_readable_code("abcdefghjklm", 12));

    // The previous 32/64-character hex formats are recognized so they can be rotated.
    CHECK(is_legacy_hex_secret(std::string(32, 'a'), 32));
    CHECK(is_legacy_hex_secret("0123456789abcdef0123456789abcdef", 32));
    CHECK(!is_legacy_hex_secret(std::string(31, 'a'), 32));
    CHECK(!is_legacy_hex_secret(std::string(32, 'A'), 32));
    CHECK(!is_legacy_hex_secret(key, 32));
    CHECK(!is_legacy_hex_secret(key, 12));

    // What to do with the stored one-time owner code when the device is not enrolled: keep a good
    // one, replace the old hex format, and create a new one for anything missing or damaged, so
    // startup never fails (and HTTP never stays closed) because of a bad stored code.
    CHECK(plan_setup_code(false, "") == SetupCodeAction::create);
    CHECK(plan_setup_code(true, owner) == SetupCodeAction::keep);
    CHECK(plan_setup_code(true, std::string(64, 'a')) == SetupCodeAction::rotate);
    CHECK(
        plan_setup_code(true, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef") ==
        SetupCodeAction::rotate);
    CHECK(plan_setup_code(true, "") == SetupCodeAction::create);
    CHECK(plan_setup_code(true, "short") == SetupCodeAction::create);
    CHECK(plan_setup_code(true, std::string(16, '0')) == SetupCodeAction::create); // banned chars
    CHECK(plan_setup_code(true, std::string(17, 'A')) == SetupCodeAction::create); // wrong length
    CHECK(plan_setup_code(true, std::string(64, 'A')) == SetupCodeAction::create); // not hex
    CHECK(plan_setup_code(true, std::string(65, 'a')) == SetupCodeAction::create); // too long
    CHECK(plan_setup_code(true, std::string("ABCD\0EFGH23456789", 17)) ==
          SetupCodeAction::create); // embedded NUL
    std::cout << "PASS: readable setup codes and legacy hex detection\n";
}
