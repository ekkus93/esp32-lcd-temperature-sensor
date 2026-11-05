#include "nvs.h"
#include "settings_codec.hpp"
#include "settings_service.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
namespace {
std::string durable;
bool namespace_exists = false;
bool fail_open = false, fail_set = false, fail_commit = false;
int fail_get_call = 0;
int get_calls = 0;
std::map<nvs_handle_t, std::string> handles;
std::map<nvs_handle_t, std::string> staged;
uint32_t next_handle = 1;
void reset(std::string bytes = {}, bool exists = false) {
    durable = std::move(bytes);
    namespace_exists = exists;
    fail_open = fail_set = fail_commit = false;
    fail_get_call = get_calls = 0;
    handles.clear();
    staged.clear();
}
uint64_t rom(unsigned serial) {
    const uint64_t value = 0x28 | (uint64_t(serial) << 8);
    uint8_t bytes[7] = {0x28, uint8_t(serial)};
    return value | (uint64_t(monitor::crc8(bytes, 7)) << 56);
}
} // namespace
extern "C" esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out) {
    if (fail_open)
        return ESP_FAIL;
    if (!namespace_exists && mode == NVS_READONLY) {
        *out = 0;
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (mode == NVS_READWRITE)
        namespace_exists = true;
    *out = next_handle++;
    handles[*out] = name;
    return ESP_OK;
}
extern "C" esp_err_t nvs_get_blob(nvs_handle_t handle, const char *, void *out, size_t *size) {
    ++get_calls;
    if (get_calls == fail_get_call)
        return ESP_FAIL;
    if (!handles.count(handle) || durable.empty())
        return ESP_ERR_NVS_NOT_FOUND;
    if (!out) {
        *size = durable.size();
        return ESP_OK;
    }
    if (*size < durable.size()) {
        *size = durable.size();
        return ESP_ERR_INVALID_SIZE;
    }
    std::memcpy(out, durable.data(), durable.size());
    *size = durable.size();
    return ESP_OK;
}
extern "C" esp_err_t nvs_set_blob(nvs_handle_t handle, const char *, const void *data,
                                  size_t size) {
    if (fail_set || !handles.count(handle))
        return ESP_FAIL;
    staged[handle] = std::string(static_cast<const char *>(data), size);
    return ESP_OK;
}
extern "C" esp_err_t nvs_commit(nvs_handle_t handle) {
    if (fail_commit || !staged.count(handle))
        return ESP_FAIL;
    durable = staged[handle];
    namespace_exists = true;
    return ESP_OK;
}
extern "C" void nvs_close(nvs_handle_t handle) {
    handles.erase(handle);
    staged.erase(handle);
}
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
int main() {
    const auto check_monitoring_survives = [](Registry &registry, unsigned serial) {
        const auto probe = rom(serial);
        CHECK(registry.discover(probe));
        registry.sample({Reading{probe, 21.5, true}}, 5000);
        CHECK(registry.sensors().size() == 1);
        CHECK(registry.sensors()[0].rom == probe);
        CHECK(registry.sensors()[0].temperature == 21.5);
        CHECK(registry.sensors()[0].state == State::normal);
    };
    reset();
    SettingsService missing("test");
    Registry empty;
    CHECK(missing.load(empty) == ESP_ERR_NVS_NOT_FOUND && missing.writable());
    empty.discover(rom(1));
    CHECK(missing.save(empty) == ESP_OK && !durable.empty());
    const auto committed = durable;

    reset("corrupt-settings-v99", true);
    SettingsService corrupt("test");
    Registry untouched;
    untouched.discover(rom(8));
    CHECK(corrupt.load(untouched) == ESP_ERR_INVALID_CRC && !corrupt.writable());
    CHECK(untouched.sensors().size() == 1 && untouched.sensors()[0].rom == rom(8));
    CHECK(corrupt.save(untouched) == ESP_ERR_INVALID_STATE);
    CHECK(durable == "corrupt-settings-v99");
    check_monitoring_survives(untouched, 8);

    const std::string unsupported = "TMS2 0\n";
    uint32_t checksum = 2166136261u;
    for (unsigned char byte : unsupported) {
        checksum ^= byte;
        checksum *= 16777619u;
    }
    const auto unsupported_bytes = unsupported + std::to_string(checksum);
    reset(unsupported_bytes, true);
    SettingsService future_version("test");
    Registry future_untouched;
    CHECK(future_version.load(future_untouched) == ESP_ERR_INVALID_CRC);
    CHECK(!future_version.writable() && durable == unsupported_bytes);
    check_monitoring_survives(future_untouched, 10);

    for (int failed_read_call : {1, 2}) {
        reset(committed, true);
        fail_get_call = failed_read_call;
        SettingsService unreadable("test");
        Registry read_untouched;
        read_untouched.discover(rom(9));
        CHECK(unreadable.load(read_untouched) == ESP_FAIL && !unreadable.writable());
        CHECK(read_untouched.sensors().size() == 1 && read_untouched.sensors()[0].rom == rom(9));
        CHECK(unreadable.save(read_untouched) == ESP_ERR_INVALID_STATE);
        CHECK(durable == committed);
        check_monitoring_survives(read_untouched, 9);
    }

    for (const auto mode : {0, 1, 2}) {
        reset(committed, true);
        SettingsService service("test");
        Registry registry;
        CHECK(service.load(registry) == ESP_OK && service.writable());
        const auto committed_registry = registry;
        auto candidate = registry;
        const auto original_name = candidate.sensors()[0].settings.name;
        auto updated = candidate.sensors()[0].settings;
        updated.name = "Uncommitted candidate";
        CHECK(candidate.configure(candidate.sensors()[0].rom, updated));
        fail_open = mode == 0;
        fail_set = mode == 1;
        fail_commit = mode == 2;
        CHECK(service.save_or_restore(candidate, committed_registry) != ESP_OK);
        // A failed write is not permanent: the store stays writable so the next change retries.
        CHECK(service.writable());
        CHECK(candidate.sensors()[0].settings.name == original_name);
        CHECK(durable == committed);
        CHECK(service.save(candidate) == ESP_OK); // unchanged content: nothing to write
        CHECK(durable == committed);
        fail_open = fail_set = fail_commit = false;
        {
            auto retried = candidate;
            auto retry_settings = retried.sensors()[0].settings;
            retry_settings.name = "Saved on retry";
            CHECK(retried.configure(retried.sensors()[0].rom, retry_settings));
            CHECK(service.save(retried) == ESP_OK);
            CHECK(durable != committed);
        }
        reset(committed, true);
        SettingsService recovered_service("test");
        Registry recovered_registry;
        CHECK(recovered_service.load(recovered_registry) == ESP_OK && recovered_service.writable());
        CHECK(recovered_registry.sensors()[0].settings.name == original_name);
        CHECK(durable == committed);
    }
    reset(committed, true);
    fail_open = true;
    SettingsService unavailable("test");
    Registry uninitialized;
    CHECK(unavailable.load(uninitialized) == ESP_FAIL && !unavailable.writable());
    CHECK(durable == committed);
    check_monitoring_survives(uninitialized, 11);
    std::cout << "PASS: sensor settings missing/corrupt/future preservation and NVS "
                 "read/open/set/commit failures\n";
}
