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

    // Length, alphabet and failure handling.
    uint8_t counter = 0;
    auto counting = [&](uint8_t *out, size_t size) {
        for (size_t i = 0; i < size; ++i)
            out[i] = counter++;
        return true;
    };
    const auto key = make_readable_code(wifi_setup_key_length, counting);
    CHECK(key.size() == wifi_setup_key_length);
    for (char c : key)
        CHECK(alphabet.find(c) != std::string::npos);
    CHECK(wifi_setup_key_length >= 8 && wifi_setup_key_length <= 16); // WPA2 needs 8-63
    CHECK(make_readable_code(0, counting).empty());
    CHECK(make_readable_code(65, counting).empty());
    CHECK(make_readable_code(12, [](uint8_t *, size_t) { return false; }).empty());
    CHECK(make_readable_code(12, nullptr).empty());

    // The previous 32-character hex format is recognized so it can be rotated.
    CHECK(is_legacy_hex_secret(std::string(32, 'a'), 32));
    CHECK(is_legacy_hex_secret("0123456789abcdef0123456789abcdef", 32));
    CHECK(!is_legacy_hex_secret(std::string(31, 'a'), 32));
    CHECK(!is_legacy_hex_secret(std::string(32, 'A'), 32));
    CHECK(!is_legacy_hex_secret(key, 32));

    std::cout << "PASS: readable code alphabet, generation and legacy format\n";
    return 0;
}
