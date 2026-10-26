#include "auth_service.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "psa/crypto.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace monitor {
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
esp_err_t AuthService::initialize() {
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
        if (result == ESP_ERR_NVS_NOT_FOUND) {
            setup_ = secure_token(32);
            if (setup_.size() != 64)
                result = ESP_FAIL;
            else {
                result = nvs_set_str(handle, "bootstrap", setup_.c_str());
                if (result == ESP_OK)
                    result = nvs_commit(handle);
            }
        } else if (result == ESP_OK && size == 65)
            setup_ = secret;
        else
            result = ESP_ERR_INVALID_STATE;
    } else
        result = ESP_OK;
    nvs_close(handle);
    ready_ = result == ESP_OK;
    return result;
}
bool AuthService::derive(const std::string &password, const std::array<uint8_t, 16> &salt,
                         std::array<uint8_t, 32> &out, uint32_t iterations) {
    auto started = esp_timer_get_time();
    psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
    auto status = psa_key_derivation_setup(&op, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_input_integer(&op, PSA_KEY_DERIVATION_INPUT_COST, iterations);
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SALT, salt.data(),
                                                salt.size());
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_PASSWORD,
                                                reinterpret_cast<const uint8_t *>(password.data()),
                                                password.size());
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_output_bytes(&op, out.data(), out.size());
    psa_key_derivation_abort(&op);
    if (status != PSA_SUCCESS)
        ESP_LOGE("auth", "Password KDF unavailable: %ld", static_cast<long>(status));
    if (iterations > 1)
        ESP_LOGI("auth", "PBKDF2 iterations=%u elapsed_ms=%lld", unsigned(iterations),
                 static_cast<long long>((esp_timer_get_time() - started) / 1000));
    return status == PSA_SUCCESS;
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
        sessions_.clear();
    }
    return ok;
}
bool AuthService::enroll(const std::string &secret, const std::string &password, int64_t now_ms) {
    if (!ready_ || enrolled_ || now_ms < next_login_)
        return false;
    next_login_ = now_ms + 5000;
    if (!constant_equal(secret, setup_))
        return false;
    bool ok = save_password(password);
    if (ok)
        next_login_ = now_ms;
    return ok;
}
bool AuthService::check_password(const std::string &password) {
    if (!ready_ || !enrolled_ || password.size() > 128)
        return false;
    std::array<uint8_t, 32> candidate{};
    if (!derive(password, salt_, candidate))
        return false;
    volatile unsigned diff = 0;
    for (size_t i = 0; i < hash_.size(); ++i)
        diff = diff | (candidate[i] ^ hash_[i]);
    std::fill(candidate.begin(), candidate.end(), 0);
    return diff == 0;
}
Session *AuthService::login(const std::string &password, int64_t now_ms) {
    if (now_ms < next_login_)
        return nullptr;
    next_login_ = now_ms + 5000;
    const auto started = esp_timer_get_time() / 1000;
    if (!check_password(password)) {
        const auto completed = now_ms + (esp_timer_get_time() / 1000 - started);
        next_login_ = completed + std::min<int64_t>(60000, 5000LL << std::min(failures_++, 4u));
        return nullptr;
    }
    failures_ = 0;
    next_login_ = now_ms + (esp_timer_get_time() / 1000 - started) + 500;
    return sessions_.create(secure_token(32), secure_token(24),
                            now_ms + (esp_timer_get_time() / 1000 - started));
}
Session *AuthService::authorize(const std::string &token, int64_t now_ms) {
    return sessions_.authorize(token, now_ms);
}
void AuthService::logout(const std::string &token) {
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
