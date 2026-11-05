#include "ota_image_check.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
static std::array<uint8_t, ota_check_prefix_bytes> image(const std::string &name,
                                                         uint32_t magic = 0xABCD5432u) {
    std::array<uint8_t, ota_check_prefix_bytes> bytes{};
    bytes[0] = 0xE9; // image header magic
    for (size_t i = 0; i < 4; ++i)
        bytes[ota_app_desc_offset + i] = uint8_t(magic >> (8 * i));
    for (size_t i = 0; i < name.size() && i < ota_project_name_size; ++i)
        bytes[ota_project_name_offset + i] = uint8_t(name[i]);
    return bytes;
}
int main() {
    const char *project = "temperature_monitor";
    auto good = image(project);
    CHECK(ota_image_matches_project(good.data(), good.size(), project));
    // Another product, a prefix of the name, a longer name and a bad descriptor are refused.
    auto other = image("some_other_product");
    CHECK(!ota_image_matches_project(other.data(), other.size(), project));
    auto prefix = image("temperature");
    CHECK(!ota_image_matches_project(prefix.data(), prefix.size(), project));
    auto longer = image("temperature_monitor_v2");
    CHECK(!ota_image_matches_project(longer.data(), longer.size(), project));
    auto no_magic = image(project, 0);
    CHECK(!ota_image_matches_project(no_magic.data(), no_magic.size(), project));
    // A name with no terminator inside its field is refused.
    auto unterminated = image(std::string(ota_project_name_size, 'x'));
    CHECK(!ota_image_matches_project(unterminated.data(), unterminated.size(), project));
    // A prefix that is too short to decide is refused.
    CHECK(!ota_image_matches_project(good.data(), good.size() - 1, project));
    CHECK(!ota_image_matches_project(good.data(), 0, project));
    std::cout << "PASS: OTA image project-name check\n";
}
