#include "auth_service.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "readable_code.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace monitor {
namespace {
struct DeriveRequest {
    const std::string *password;
    const std::array<uint8_t, 16> *salt;
    std::array<uint8_t, 32> *out;
    uint32_t iterations;
    // Given once when the worker is done. It belongs to this request alone, so nothing else that
    // notifies the waiting task (or a stray notification) can release the wait early.
    SemaphoreHandle_t done;
    bool result = false;
};

// Holds a mutex for one short operation.
struct Guard {
    explicit Guard(SemaphoreHandle_t mutex) : mutex_(mutex) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    ~Guard() {
        xSemaphoreGive(mutex_);
    }
    SemaphoreHandle_t mutex_;
};

void derive_worker(void *context) {
    auto &request = *static_cast<DeriveRequest *>(context);
    psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
    auto status = psa_key_derivation_setup(&op, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_input_integer(&op, PSA_KEY_DERIVATION_INPUT_COST,
                                                  request.iterations);
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SALT,
                                                request.salt->data(), request.salt->size());
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_input_bytes(
            &op, PSA_KEY_DERIVATION_INPUT_PASSWORD,
            reinterpret_cast<const uint8_t *>(request.password->data()), request.password->size());
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_output_bytes(&op, request.out->data(), request.out->size());
    psa_key_derivation_abort(&op);
    request.result = status == PSA_SUCCESS;
    xSemaphoreGive(request.done);
    // Wait here to be deleted: the stack was allocated with caps, so the creator frees it with
    // vTaskDeleteWithCaps() once this task is suspended.
    vTaskSuspend(nullptr);
}
} // namespace

std::string secure_token(size_t size) {
    if (size > 64)
        return {};
    std::array<uint8_t, 64> bytes{};
    if (psa_generate_random(bytes.data(), size) != PSA_SUCCESS)
        return {};
    std::string text(size * 2, '0');
    constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) {
        text[2 * i] = hex[bytes[i] >> 4];
        text[2 * i + 1] = hex[bytes[i] & 15];
    }
    return text;
}
std::string readable_secret(size_t length) {
    return make_readable_code(length, [](uint8_t *bytes, size_t size) {
        return psa_generate_random(bytes, size) == PSA_SUCCESS;
    });
}
esp_err_t AuthService::initialize() {
    if (!mutex_)
        mutex_ = xSemaphoreCreateMutex();
    if (!kdf_mutex_)
        kdf_mutex_ = xSemaphoreCreateMutex();
    if (!mutex_ || !kdf_mutex_)
        return ESP_ERR_NO_MEM;
    if (psa_crypto_init() != PSA_SUCCESS)
        return ESP_FAIL;
    std::array<uint8_t, 16> test_salt{};
    std::array<uint8_t, 32> test_result{};
    const std::array<uint8_t, 32> expected = {5,   150, 78,  59,  102, 199, 203, 12,  129, 136, 253,
                                              142, 92,  213, 135, 141, 244, 143, 45,  233, 238, 1,
                                              92,  37,  242, 74,  12,  239, 177, 181, 136, 238};
    if (!derive("device-crypto-selftest", test_salt, test_result, 1) || test_result != expected)
        return ESP_FAIL;
    ESP_LOGI("auth", "SELFTEST PASS PBKDF2-HMAC-SHA256 known answer");
    nvs_handle_t handle;
    auto result = nvs_open("monitor_auth", NVS_READWRITE, &handle);
    if (result != ESP_OK)
        return result;
    size_t size = 52;
    std::array<uint8_t, 52> record{};
    result = nvs_get_blob(handle, "verifier2", record.data(), &size);
    if (result == ESP_OK && size == record.size()) {
        rounds_ = uint32_t(record[0]) | uint32_t(record[1]) << 8 | uint32_t(record[2]) << 16 |
                  uint32_t(record[3]) << 24;
        std::copy_n(record.begin() + 4, 16, salt_.begin());
        std::copy_n(record.begin() + 20, 32, hash_.begin());
        enrolled_ = true;
    } else if (result == ESP_ERR_NVS_NOT_FOUND) {
        // A password saved before the round count was stored: it still works at the old count.
        size = 48;
        std::array<uint8_t, 48> legacy{};
        result = nvs_get_blob(handle, "verifier1", legacy.data(), &size);
        if (result == ESP_OK && size == legacy.size()) {
            std::copy_n(legacy.begin(), 16, salt_.begin());
            std::copy_n(legacy.begin() + 16, 32, hash_.begin());
            rounds_ = legacy_password_rounds;
            legacy_ = true;
            enrolled_ = true;
        } else if (result != ESP_ERR_NVS_NOT_FOUND) {
            nvs_close(handle);
            return result == ESP_OK ? ESP_ERR_INVALID_SIZE : result;
        }
    } else {
        nvs_close(handle);
        return result == ESP_OK ? ESP_ERR_INVALID_SIZE : result;
    }
    // Not enrolled yet is a normal state: the administrator is created during setup.
    if (result == ESP_ERR_NVS_NOT_FOUND)
        result = ESP_OK;
    // Older firmware stored a one-time owner code under "bootstrap". It is no longer used; remove
    // it so no secret is left in storage. Failing to remove it does not stop startup.
    if (result == ESP_OK) {
        const auto stale = nvs_erase_key(handle, "bootstrap");
        if (stale == ESP_OK && nvs_commit(handle) == ESP_OK)
            ESP_LOGI("auth", "Removed the unused owner setup code from storage");
        else if (stale != ESP_ERR_NVS_NOT_FOUND)
            ESP_LOGW("auth", "Could not remove the unused owner setup code (error %d)",
                     int(stale == ESP_OK ? ESP_FAIL : stale));
    }
    nvs_close(handle);
    ready_ = result == ESP_OK;
    return result;
}
bool AuthService::derive(const std::string &password, const std::array<uint8_t, 16> &salt,
                         std::array<uint8_t, 32> &out, uint32_t iterations) {
    [[maybe_unused]] const auto started = esp_timer_get_time(); // only used by the timing log
    DeriveRequest request{&password, &salt, &out, iterations, xSemaphoreCreateBinary()};
    if (!request.done)
        return false;
    // The HTTP task is pinned to core 1. A priority-0 worker shares that core
    // with IDLE1, while higher-priority network tasks can still preempt it.
    // Only one derivation at a time: each needs a large stack and working memory.
    // The stack comes from PSRAM: internal RAM is mostly the fragmented reserve kept for Wi-Fi and
    // DMA, and an 8 KiB block there was often unavailable, so logins failed at random. The worker
    // only computes (no flash access), and the SHA driver copies PSRAM buffers for its DMA.
    xSemaphoreTake(kdf_mutex_, portMAX_DELAY);
    TaskHandle_t worker = nullptr;
    if (xTaskCreatePinnedToCoreWithCaps(derive_worker, "password_kdf", 8192, &request,
                                        tskIDLE_PRIORITY, &worker, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE("auth", "Could not start password KDF worker");
        xSemaphoreGive(kdf_mutex_);
        vSemaphoreDelete(request.done);
        return false;
    }
    xSemaphoreTake(request.done, portMAX_DELAY);
    while (eTaskGetState(worker) != eSuspended)
        vTaskDelay(1);
    vTaskDeleteWithCaps(worker);
    xSemaphoreGive(kdf_mutex_);
    vSemaphoreDelete(request.done);
    if (!request.result)
        ESP_LOGE("auth", "Password KDF unavailable");
    if (iterations > 1)
        ESP_LOGI("auth", "PBKDF2 iterations=%u elapsed_ms=%lld", unsigned(iterations),
                 static_cast<long long>((esp_timer_get_time() - started) / 1000));
    return request.result;
}
bool AuthService::erase_stored_password() {
    nvs_handle_t handle;
    if (nvs_open("monitor_auth", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    const auto current = nvs_erase_key(handle, "verifier2");
    const auto legacy = nvs_erase_key(handle, "verifier1");
    const bool ok = (current == ESP_OK || current == ESP_ERR_NVS_NOT_FOUND) &&
                    (legacy == ESP_OK || legacy == ESP_ERR_NVS_NOT_FOUND) &&
                    nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}
bool AuthService::save_password(const std::string &password) {
    if (password.size() < 12 || password.size() > 128)
        return false;
    std::array<uint8_t, 16> salt{};
    std::array<uint8_t, 32> hash{};
    if (psa_generate_random(salt.data(), salt.size()) != PSA_SUCCESS ||
        !derive(password, salt, hash, password_rounds))
        return false;
    std::array<uint8_t, 52> record{};
    for (size_t i = 0; i < 4; ++i)
        record[i] = uint8_t(password_rounds >> (8 * i));
    std::copy(salt.begin(), salt.end(), record.begin() + 4);
    std::copy(hash.begin(), hash.end(), record.begin() + 20);
    nvs_handle_t handle;
    if (nvs_open("monitor_auth", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    // The new record replaces any legacy one in the same commit.
    const auto legacy = nvs_erase_key(handle, "verifier1");
    const bool ok = nvs_set_blob(handle, "verifier2", record.data(), record.size()) == ESP_OK &&
                    (legacy == ESP_OK || legacy == ESP_ERR_NVS_NOT_FOUND) &&
                    nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    if (ok) {
        Guard guard(mutex_);
        salt_ = salt;
        hash_ = hash;
        rounds_ = password_rounds;
        legacy_ = false;
        enrolled_ = true;
        sessions_.clear();
    }
    return ok;
}
AuthService::EnrollResult AuthService::enroll(const std::string &password, int64_t now_ms,
                                              bool setup_window_open) {
    {
        Guard guard(mutex_);
        if (!ready_)
            return EnrollResult::unavailable;
        if (enrolled_)
            return EnrollResult::already_enrolled;
        if (!setup_window_open)
            return EnrollResult::window_closed;
        if (password.size() < 12 || password.size() > 128)
            return EnrollResult::bad_password;
        if (now_ms < next_login_)
            return EnrollResult::too_soon;
        next_login_ = now_ms + 5000;
    }
    const bool ok = save_password(password);
    if (ok) {
        Guard guard(mutex_);
        next_login_ = now_ms;
    }
    return ok ? EnrollResult::enrolled : EnrollResult::unavailable;
}
EnrollReply enrollment_reply(AuthService::EnrollResult result) {
    using Result = AuthService::EnrollResult;
    switch (result) {
    case Result::enrolled:
        return {"201 Created", "", 0};
    case Result::already_enrolled:
        return {"409 Conflict", "An administrator password already exists", 0};
    case Result::window_closed:
        return {"403 Forbidden", "Hold BOOT for five seconds to open setup, then try again", 0};
    case Result::too_soon:
        return {"429 Too Many Requests", "Wait five seconds and try again", 5};
    case Result::bad_password:
        return {"400 Bad Request", "The password must be 12 to 128 characters", 0};
    case Result::unavailable:
        break;
    }
    return {"503 Service Unavailable",
            "The device could not save the password just now; try again in a few seconds", 5};
}
AuthService::PasswordCheck AuthService::check_password_result(const std::string &password) {
    if (!ready_ || !enrolled_ || password.size() > 128)
        return PasswordCheck::mismatch;
    // Work on copies: the slow derive must not hold the lock, and the stored verifier can be
    // replaced by a password change on another task while it runs.
    std::array<uint8_t, 16> salt{};
    std::array<uint8_t, 32> stored{};
    uint32_t rounds = 0;
    {
        Guard guard(mutex_);
        salt = salt_;
        stored = hash_;
        rounds = rounds_;
    }
    std::array<uint8_t, 32> candidate{};
    if (!derive(password, salt, candidate, rounds))
        return PasswordCheck::kdf_failed;
    volatile unsigned diff = 0;
    for (size_t i = 0; i < stored.size(); ++i)
        diff = diff | (candidate[i] ^ stored[i]);
    std::fill(candidate.begin(), candidate.end(), 0);
    return diff == 0 ? PasswordCheck::match : PasswordCheck::mismatch;
}
LoginThrottle::Decision AuthService::login_begin(uint32_t client, int64_t now_ms) {
    Guard guard(mutex_);
    return throttle_.begin(client, now_ms);
}
void AuthService::login_abort(uint32_t client) {
    Guard guard(mutex_);
    throttle_.abort(client);
}
std::optional<Session> AuthService::login_complete(const std::string &password, uint32_t client,
                                                   int64_t now_ms, LoginFailure *failure) {
    const auto started = esp_timer_get_time() / 1000;
    const auto check = check_password_result(password); // slow; no lock held
    std::string token, csrf;
    LoginOutcome outcome = check == PasswordCheck::match        ? LoginOutcome::verified
                           : check == PasswordCheck::kdf_failed ? LoginOutcome::kdf_error
                                                                : LoginOutcome::wrong_password;
    if (outcome == LoginOutcome::verified) {
        // A password saved at the old round count is moved to the current one now, while the
        // plain password is in hand. A failed upgrade keeps the old record: the login still works.
        bool upgrade = false;
        {
            Guard guard(mutex_);
            upgrade = legacy_;
        }
        if (upgrade)
            save_password(password);
        token = secure_token(32);
        csrf = secure_token(24);
        if (token.empty() || csrf.empty())
            outcome = LoginOutcome::kdf_error; // no entropy: a device failure, not a bad password
    }
    const auto finished_ms = now_ms + (esp_timer_get_time() / 1000 - started);
    Guard guard(mutex_);
    throttle_.finish(client, outcome, finished_ms);
    if (failure)
        *failure = outcome == LoginOutcome::wrong_password ? LoginFailure::wrong_password
                                                           : LoginFailure::unavailable;
    if (outcome != LoginOutcome::verified)
        return std::nullopt;
    if (auto *session = sessions_.create(token, csrf, finished_ms)) {
        if (failure)
            *failure = LoginFailure::none;
        return *session;
    }
    return std::nullopt;
}
std::optional<Session> AuthService::authorize(const std::string &token, int64_t now_ms) {
    Guard guard(mutex_);
    if (auto *session = sessions_.authorize(token, now_ms))
        return *session;
    return std::nullopt;
}
void AuthService::logout(const std::string &token) {
    Guard guard(mutex_);
    sessions_.logout(token);
}

bool AuthService::change_password(const std::string &token, const std::string &old_password,
                                  const std::string &new_password, int64_t now_ms) {
    if (!authorize(token, now_ms))
        return false;
    {
        Guard guard(mutex_);
        if (now_ms < next_login_)
            return false;
    }
    const auto started = esp_timer_get_time() / 1000;
    bool ok = check_password(old_password) && save_password(new_password);
    Guard guard(mutex_);
    next_login_ = now_ms + (esp_timer_get_time() / 1000 - started) + (ok ? 500 : 5000);
    return ok;
}
} // namespace monitor
