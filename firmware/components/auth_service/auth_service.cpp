#include "auth_service.hpp"
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
    TaskHandle_t caller;
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
    xTaskNotifyGive(request.caller);
    vTaskDelete(nullptr);
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
namespace {
// Generates and persists a new owner setup code; leaves `code` untouched on failure.
esp_err_t store_new_setup_code(nvs_handle_t handle, std::string &code) {
    auto fresh = readable_secret(owner_setup_code_length);
    if (fresh.size() != owner_setup_code_length)
        return ESP_FAIL;
    auto result = nvs_set_str(handle, "bootstrap", fresh.c_str());
    if (result == ESP_OK)
        result = nvs_commit(handle);
    if (result == ESP_OK)
        code = std::move(fresh);
    return result;
}
} // namespace
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
    size_t size = 48;
    std::array<uint8_t, 48> record{};
    result = nvs_get_blob(handle, "verifier1", record.data(), &size);
    if (result == ESP_OK && size == record.size()) {
        std::copy_n(record.begin(), 16, salt_.begin());
        std::copy_n(record.begin() + 16, 32, hash_.begin());
        enrolled_ = true;
    } else if (result != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return result == ESP_OK ? ESP_ERR_INVALID_SIZE : result;
    }
    if (!enrolled_) {
        char secret[65]{};
        size = sizeof(secret);
        result = nvs_get_str(handle, "bootstrap", secret, &size);
        const bool stored = result == ESP_OK;
        // A missing, oversized or damaged code is replaced rather than failing startup: failing
        // here would keep HTTP closed and leave no way to set the device up.
        if (result == ESP_ERR_NVS_NOT_FOUND || result == ESP_ERR_NVS_INVALID_LENGTH || stored) {
            switch (plan_setup_code(stored, stored ? std::string(secret) : std::string())) {
            case SetupCodeAction::keep:
                setup_ = secret;
                result = ESP_OK;
                break;
            case SetupCodeAction::rotate:
                // The old code still works if the replacement cannot be stored.
                setup_ = secret;
                if (store_new_setup_code(handle, setup_) != ESP_OK)
                    ESP_LOGW("auth",
                             "Could not shorten the owner setup code; keeping the existing one");
                else
                    ESP_LOGI("auth", "Owner setup code replaced with a shorter format");
                result = ESP_OK;
                break;
            case SetupCodeAction::create:
                if (result != ESP_ERR_NVS_NOT_FOUND)
                    ESP_LOGW("auth", "Stored owner setup code is damaged; creating a new one");
                result = store_new_setup_code(handle, setup_);
                break;
            }
        }
    } else
        result = ESP_OK;
    nvs_close(handle);
    ready_ = result == ESP_OK;
    return result;
}
bool AuthService::derive(const std::string &password, const std::array<uint8_t, 16> &salt,
                         std::array<uint8_t, 32> &out, uint32_t iterations) {
    auto started = esp_timer_get_time();
    DeriveRequest request{&password, &salt, &out, iterations, xTaskGetCurrentTaskHandle()};
    // The HTTP task is pinned to core 1. A priority-0 worker shares that core
    // with IDLE1, while higher-priority network tasks can still preempt it.
    // Only one derivation at a time: each needs a large stack and working memory.
    xSemaphoreTake(kdf_mutex_, portMAX_DELAY);
    TaskHandle_t worker = nullptr;
    if (xTaskCreatePinnedToCore(derive_worker, "password_kdf", 8192, &request, tskIDLE_PRIORITY,
                                &worker, 1) != pdPASS) {
        ESP_LOGE("auth", "Could not start password KDF worker");
        xSemaphoreGive(kdf_mutex_);
        return false;
    }
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    xSemaphoreGive(kdf_mutex_);
    if (!request.result)
        ESP_LOGE("auth", "Password KDF unavailable");
    if (iterations > 1)
        ESP_LOGI("auth", "PBKDF2 iterations=%u elapsed_ms=%lld", unsigned(iterations),
                 static_cast<long long>((esp_timer_get_time() - started) / 1000));
    return request.result;
}
bool AuthService::save_password(const std::string &password) {
    if (password.size() < 12 || password.size() > 128)
        return false;
    std::array<uint8_t, 16> salt{};
    std::array<uint8_t, 32> hash{};
    if (psa_generate_random(salt.data(), salt.size()) != PSA_SUCCESS ||
        !derive(password, salt, hash))
        return false;
    std::array<uint8_t, 48> record{};
    std::copy(salt.begin(), salt.end(), record.begin());
    std::copy(hash.begin(), hash.end(), record.begin() + 16);
    nvs_handle_t handle;
    if (nvs_open("monitor_auth", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    bool ok = nvs_set_blob(handle, "verifier1", record.data(), record.size()) == ESP_OK &&
              nvs_commit(handle) == ESP_OK;
    if (ok) {
        nvs_erase_key(handle, "bootstrap");
        nvs_commit(handle);
    }
    nvs_close(handle);
    if (ok) {
        salt_ = salt;
        hash_ = hash;
        enrolled_ = true;
        setup_.clear();
        Guard guard(mutex_);
        sessions_.clear();
    }
    return ok;
}
AuthService::EnrollResult AuthService::enroll(const std::string &secret,
                                              const std::string &password, int64_t now_ms) {
    if (!ready_ || enrolled_ || now_ms < next_login_)
        return EnrollResult::denied;
    // A code in the current format is compared after normalising what was typed (case, spaces,
    // hyphens). A malformed one is reported as such and is not a guess; the old 64-character hex
    // code, if still stored, is compared exactly.
    std::string candidate = secret;
    if (is_readable_code(setup_, owner_setup_code_length)) {
        const auto normalized = normalize_owner_code(secret);
        if (!normalized)
            return EnrollResult::bad_format;
        candidate = *normalized;
    }
    next_login_ = now_ms + 5000;
    if (!constant_equal(candidate, setup_))
        return EnrollResult::denied;
    const bool ok = save_password(password);
    if (ok)
        next_login_ = now_ms;
    return ok ? EnrollResult::enrolled : EnrollResult::denied;
}
AuthService::PasswordCheck AuthService::check_password_result(const std::string &password) {
    if (!ready_ || !enrolled_ || password.size() > 128)
        return PasswordCheck::mismatch;
    std::array<uint8_t, 32> candidate{};
    if (!derive(password, salt_, candidate))
        return PasswordCheck::kdf_failed;
    volatile unsigned diff = 0;
    for (size_t i = 0; i < hash_.size(); ++i)
        diff = diff | (candidate[i] ^ hash_[i]);
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
                                                   int64_t now_ms) {
    const auto started = esp_timer_get_time() / 1000;
    const auto check = check_password_result(password); // slow; no lock held
    std::string token, csrf;
    LoginOutcome outcome = check == PasswordCheck::match        ? LoginOutcome::verified
                           : check == PasswordCheck::kdf_failed ? LoginOutcome::kdf_error
                                                                : LoginOutcome::wrong_password;
    if (outcome == LoginOutcome::verified) {
        token = secure_token(32);
        csrf = secure_token(24);
        if (token.empty() || csrf.empty())
            outcome = LoginOutcome::kdf_error; // no entropy: a device failure, not a bad password
    }
    const auto finished_ms = now_ms + (esp_timer_get_time() / 1000 - started);
    Guard guard(mutex_);
    throttle_.finish(client, outcome, finished_ms);
    if (outcome != LoginOutcome::verified)
        return std::nullopt;
    if (auto *session = sessions_.create(token, csrf, finished_ms))
        return *session;
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
    if (!authorize(token, now_ms) || now_ms < next_login_)
        return false;
    const auto started = esp_timer_get_time() / 1000;
    bool ok = check_password(old_password) && save_password(new_password);
    next_login_ = now_ms + (esp_timer_get_time() / 1000 - started) + (ok ? 500 : 5000);
    return ok;
}
} // namespace monitor
