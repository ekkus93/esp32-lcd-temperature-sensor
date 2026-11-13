#include "nvs.h"
#include "sensor_settings_persistence.hpp"
#include "settings_codec.hpp"
#include "settings_service.hpp"
#include <algorithm>
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
int open_calls = 0;
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
    ++open_calls;
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

    // Review audit F3: the main loop's policy. A failed write rolls the edit back and pauses edits;
    // a later edit is stored without a restart. Corrupt stored data stays read-only.
    const auto stored_name = []() {
        const auto decoded = decode_settings(durable);
        CHECK(decoded && !decoded->empty());
        return (*decoded)[0].settings.name;
    };
    const auto edit = [](Registry &registry, const char *name) {
        auto updated = registry.sensors()[0].settings;
        updated.name = name;
        CHECK(registry.configure(registry.sensors()[0].rom, updated));
    };
    for (const auto mode : {0, 1, 2}) {
        reset(committed, true);
        SettingsService service("test");
        Registry registry;
        CHECK(service.load(registry) == ESP_OK);
        SensorSettingsPersistence persistence(service);
        const int64_t t0 = 1000000;
        CHECK(persistence.accepting_edits(t0));
        const auto committed_registry = registry;
        const auto committed_revision = registry.sensors()[0].config_revision;
        edit(registry, "First edit");
        CHECK(registry.sensors()[0].config_revision != committed_revision);
        fail_open = mode == 0;
        fail_set = mode == 1;
        fail_commit = mode == 2;
        CHECK(persistence.commit_edits(registry, committed_registry, t0) != ESP_OK);
        fail_open = fail_set = fail_commit = false;
        // Rolled back, revision included; nothing reached storage; edits pause, then resume.
        CHECK(registry.sensors()[0].settings.name == committed_registry.sensors()[0].settings.name);
        CHECK(registry.sensors()[0].config_revision == committed_revision);
        CHECK(durable == committed);
        CHECK(!persistence.accepting_edits(t0));
        CHECK(!persistence.accepting_edits(t0 + SensorSettingsPersistence::first_backoff_ms - 1));
        const int64_t t1 = t0 + SensorSettingsPersistence::first_backoff_ms;
        CHECK(persistence.accepting_edits(t1));
        // Acquisition carried on meanwhile.
        registry.sample({Reading{registry.sensors()[0].rom, 20.0, true}}, 5000);
        CHECK(registry.sensors()[0].temperature == 20.0);
        // The second edit is stored, durably, without a restart.
        const auto second_committed = registry;
        edit(registry, "Second edit");
        CHECK(persistence.commit_edits(registry, second_committed, t1) == ESP_OK);
        CHECK(registry.sensors()[0].settings.name == "Second edit");
        CHECK(stored_name() == "Second edit");
        CHECK(persistence.accepting_edits(t1) && !persistence.unsaved());
        SettingsService reloaded("test");
        Registry after_restart;
        CHECK(reloaded.load(after_restart) == ESP_OK && reloaded.writable());
        CHECK(after_restart.sensors()[0].settings.name == "Second edit");
    }
    // The pause doubles per consecutive failure up to ten minutes; a success clears it.
    {
        reset(committed, true);
        SettingsService service("test");
        Registry registry;
        CHECK(service.load(registry) == ESP_OK);
        SensorSettingsPersistence persistence(service);
        fail_commit = true;
        int64_t now = 0;
        int64_t expected = SensorSettingsPersistence::first_backoff_ms;
        for (int failure = 0; failure < 8; ++failure) {
            const auto before = registry;
            edit(registry, failure % 2 ? "Odd" : "Even");
            CHECK(persistence.commit_edits(registry, before, now) != ESP_OK);
            CHECK(persistence.retry_at_ms() == now + expected);
            now = persistence.retry_at_ms();
            expected = std::min(expected * 2, SensorSettingsPersistence::max_backoff_ms);
        }
        fail_commit = false;
        const auto before = registry;
        edit(registry, "Recovered");
        CHECK(persistence.commit_edits(registry, before, now) == ESP_OK);
        const auto after = registry;
        edit(registry, "Fails once");
        fail_commit = true;
        CHECK(persistence.commit_edits(registry, after, now) != ESP_OK);
        CHECK(persistence.retry_at_ms() == now + SensorSettingsPersistence::first_backoff_ms);
        fail_commit = false;
    }
    // Automatic persistence (a discovered probe) is retried after the pause, not every cycle.
    {
        reset(committed, true);
        SettingsService service("test");
        Registry registry;
        CHECK(service.load(registry) == ESP_OK);
        SensorSettingsPersistence persistence(service);
        CHECK(registry.discover(rom(5)));
        fail_set = true;
        CHECK(persistence.persist(registry, true, 0) == ESP_FAIL);
        CHECK(persistence.unsaved() && !persistence.accepting_edits(0));
        fail_set = false;
        open_calls = 0;
        for (int64_t cycle = 5000; cycle < SensorSettingsPersistence::first_backoff_ms;
             cycle += 5000)
            CHECK(persistence.persist(registry, false, cycle) == ESP_ERR_NOT_FINISHED);
        CHECK(open_calls == 0 && durable == committed);
        CHECK(persistence.persist(registry, false, SensorSettingsPersistence::first_backoff_ms) ==
              ESP_OK);
        CHECK(!persistence.unsaved() && open_calls == 1);
        const auto decoded = decode_settings(durable);
        CHECK(decoded && decoded->size() == 2);
        // Nothing changed: nothing is written.
        CHECK(persistence.persist(registry, false, 1 << 30) == ESP_OK && open_calls == 1);
    }
    // Damaged stored settings stay read-only and untouched however long the device runs.
    {
        reset("corrupt-settings-v99", true);
        SettingsService service("test");
        Registry registry;
        registry.discover(rom(8));
        CHECK(service.load(registry) == ESP_ERR_INVALID_CRC);
        SensorSettingsPersistence persistence(service);
        CHECK(!persistence.accepting_edits(0) && !persistence.accepting_edits(INT64_MAX));
        CHECK(persistence.persist(registry, true, 0) == ESP_ERR_INVALID_STATE);
        const auto before = registry;
        edit(registry, "Not stored");
        CHECK(persistence.commit_edits(registry, before, 0) == ESP_ERR_INVALID_STATE);
        CHECK(registry.sensors()[0].settings.name == before.sensors()[0].settings.name);
        CHECK(durable == "corrupt-settings-v99");
        CHECK(!persistence.accepting_edits(INT64_MAX));
    }
    std::cout << "PASS: sensor settings missing/corrupt/future preservation, NVS "
                 "read/open/set/commit failures and retry after a transient failure\n";
}
