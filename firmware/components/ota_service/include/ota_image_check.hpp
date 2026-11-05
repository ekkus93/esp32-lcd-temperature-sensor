#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace monitor {
// An application image starts with esp_image_header_t (24 bytes) and a segment header (8 bytes);
// the first segment begins with esp_app_desc_t: magic_word, secure_version, reserv1[2],
// version[32], project_name[32].
constexpr size_t ota_app_desc_offset = 24 + 8;
constexpr size_t ota_project_name_offset = ota_app_desc_offset + 4 + 4 + 8 + 32;
constexpr size_t ota_project_name_size = 32;
// Bytes of the upload needed before the check can decide.
constexpr size_t ota_check_prefix_bytes = ota_project_name_offset + ota_project_name_size;

// True when the image prefix carries a valid app descriptor naming `project_name`. This stops a
// firmware built for a different product from being written to the OTA slot. It is a sanity check,
// not authentication: images are not signed and secure boot is out of scope.
inline bool ota_image_matches_project(const uint8_t *bytes, size_t size, const char *project_name) {
    if (size < ota_check_prefix_bytes)
        return false;
    uint32_t magic = 0;
    std::memcpy(&magic, bytes + ota_app_desc_offset, sizeof(magic));
    if (magic != 0xABCD5432u)
        return false;
    const char *name = reinterpret_cast<const char *>(bytes + ota_project_name_offset);
    const size_t length = strnlen(name, ota_project_name_size);
    return length < ota_project_name_size && length == std::strlen(project_name) &&
           std::memcmp(name, project_name, length) == 0;
}
} // namespace monitor
