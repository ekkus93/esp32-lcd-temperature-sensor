// Host stand-ins for the parts of ESP-IDF that AuthService uses, so its real code runs in tests.
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "psa/crypto.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <random>
#include <thread>

// ---- FreeRTOS: a semaphore is a counter behind a mutex; a task is a detached thread ----
namespace {
struct Semaphore {
    std::mutex m;
    std::condition_variable cv;
    int count;
    explicit Semaphore(int initial) : count(initial) {}
};
} // namespace
SemaphoreHandle_t xSemaphoreCreateMutex() {
    return new Semaphore(1);
}
SemaphoreHandle_t xSemaphoreCreateBinary() {
    return new Semaphore(0);
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, uint32_t) {
    auto *s = static_cast<Semaphore *>(handle);
    std::unique_lock<std::mutex> lock(s->m);
    s->cv.wait(lock, [&] { return s->count > 0; });
    s->count = 0;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
    auto *s = static_cast<Semaphore *>(handle);
    std::lock_guard<std::mutex> lock(s->m);
    s->count = 1;
    s->cv.notify_one();
    return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t handle) {
    delete static_cast<Semaphore *>(handle);
}
bool FakeTasks::fail_create = false;
uint32_t FakeTasks::last_caps = 0;
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *, uint32_t, void *arg, int,
                                   TaskHandle_t *handle, int) {
    if (FakeTasks::fail_create)
        return 0;
    if (handle)
        *handle = nullptr;
    std::thread([fn, arg] { fn(arg); }).detach();
    return pdPASS;
}
namespace {
// A caps task on the host: a thread whose function has returned once `finished` is set (the device
// worker suspends itself at that point).
struct FakeTask {
    std::atomic<bool> finished{false};
};
} // namespace
BaseType_t xTaskCreatePinnedToCoreWithCaps(void (*fn)(void *), const char *, uint32_t, void *arg,
                                           int, TaskHandle_t *handle, int, uint32_t caps) {
    FakeTasks::last_caps = caps;
    if (FakeTasks::fail_create)
        return 0;
    auto *task = new FakeTask;
    if (handle)
        *handle = task;
    std::thread([fn, arg, task] {
        fn(arg);
        task->finished = true;
    }).detach();
    return pdPASS;
}
void vTaskDelete(TaskHandle_t) {}
void vTaskDeleteWithCaps(TaskHandle_t handle) {
    delete static_cast<FakeTask *>(handle);
}
void vTaskSuspend(TaskHandle_t) {}
void vTaskDelay(uint32_t) {
    std::this_thread::yield();
}
eTaskState eTaskGetState(TaskHandle_t handle) {
    return static_cast<FakeTask *>(handle)->finished ? eSuspended : eRunning;
}

// ---- NVS: one in-memory namespace ----
namespace {
std::map<std::string, std::string> nvs_store;
std::mutex nvs_mutex;
} // namespace
bool FakeNvs::fail_open = false, FakeNvs::fail_commit = false, FakeNvs::fail_set = false;
void FakeNvs::reset() {
    std::lock_guard<std::mutex> lock(nvs_mutex);
    nvs_store.clear();
    fail_open = fail_commit = fail_set = false;
}
bool FakeNvs::has(const char *key) {
    std::lock_guard<std::mutex> lock(nvs_mutex);
    return nvs_store.count(key) != 0;
}
std::string FakeNvs::get(const char *key) {
    std::lock_guard<std::mutex> lock(nvs_mutex);
    return nvs_store.count(key) ? nvs_store[key] : std::string{};
}
void FakeNvs::put(const char *key, const std::string &value) {
    std::lock_guard<std::mutex> lock(nvs_mutex);
    nvs_store[key] = value;
}
void FakeNvs::erase(const char *key) {
    std::lock_guard<std::mutex> lock(nvs_mutex);
    nvs_store.erase(key);
}
extern "C" {
esp_err_t nvs_open(const char *, int, nvs_handle_t *out) {
    if (FakeNvs::fail_open)
        return ESP_FAIL;
    *out = 1;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t, const char *key, void *out, size_t *size) {
    std::lock_guard<std::mutex> lock(nvs_mutex);
    auto it = nvs_store.find(key);
    if (it == nvs_store.end())
        return ESP_ERR_NVS_NOT_FOUND;
    if (*size < it->second.size()) {
        *size = it->second.size();
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    std::memcpy(out, it->second.data(), it->second.size());
    *size = it->second.size();
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char *key, const void *data, size_t size) {
    if (FakeNvs::fail_set)
        return ESP_FAIL;
    FakeNvs::put(key, std::string(static_cast<const char *>(data), size));
    return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t, const char *key, char *out, size_t *size) {
    std::lock_guard<std::mutex> lock(nvs_mutex);
    auto it = nvs_store.find(key);
    if (it == nvs_store.end())
        return ESP_ERR_NVS_NOT_FOUND;
    if (*size < it->second.size() + 1) {
        *size = it->second.size() + 1;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    std::memcpy(out, it->second.c_str(), it->second.size() + 1);
    *size = it->second.size() + 1;
    return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t, const char *key, const char *value) {
    if (FakeNvs::fail_set)
        return ESP_FAIL;
    FakeNvs::put(key, value);
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t, const char *key) {
    FakeNvs::erase(key);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t) {
    return FakeNvs::fail_commit ? ESP_FAIL : ESP_OK;
}
void nvs_close(nvs_handle_t) {}
}

// ---- PSA: real PBKDF2-HMAC-SHA256 (FIPS 180-4 / RFC 2898), random from the OS ----
namespace {
struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buffer[64]{};
    uint64_t length = 0;
    size_t used = 0;
    static uint32_t rotr(uint32_t x, int n) {
        return (x >> n) | (x << (32 - n));
    }
    void block(const uint8_t *p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 |
                   uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 =
                hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 =
                (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    void update(const uint8_t *data, size_t size) {
        length += size;
        while (size) {
            const size_t take = std::min(size, sizeof(buffer) - used);
            std::memcpy(buffer + used, data, take);
            used += take;
            data += take;
            size -= take;
            if (used == 64) {
                block(buffer);
                used = 0;
            }
        }
    }
    std::array<uint8_t, 32> finish() {
        const uint64_t bits = length * 8;
        const uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (used != 56)
            update(&zero, 1);
        uint8_t tail[8];
        for (int i = 0; i < 8; ++i)
            tail[i] = uint8_t(bits >> (56 - 8 * i));
        update(tail, 8);
        std::array<uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 4; ++j)
                out[4 * i + j] = uint8_t(h[i] >> (24 - 8 * j));
        return out;
    }
};
std::array<uint8_t, 32> hmac(const std::vector<uint8_t> &key, const uint8_t *data, size_t size,
                             const uint8_t *data2 = nullptr, size_t size2 = 0) {
    std::array<uint8_t, 64> k{};
    if (key.size() > 64) {
        Sha256 s;
        s.update(key.data(), key.size());
        const auto d = s.finish();
        std::memcpy(k.data(), d.data(), 32);
    } else if (!key.empty())
        std::memcpy(k.data(), key.data(), key.size());
    std::array<uint8_t, 64> ipad{}, opad{};
    for (int i = 0; i < 64; ++i) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    Sha256 inner;
    inner.update(ipad.data(), 64);
    inner.update(data, size);
    if (data2)
        inner.update(data2, size2);
    const auto digest = inner.finish();
    Sha256 outer;
    outer.update(opad.data(), 64);
    outer.update(digest.data(), 32);
    return outer.finish();
}
} // namespace
bool psa_mock_fail = false;
psa_status_t psa_crypto_init() {
    return PSA_SUCCESS;
}
psa_status_t psa_generate_random(uint8_t *output, size_t size) {
    static std::random_device device;
    for (size_t i = 0; i < size; ++i)
        output[i] = uint8_t(device());
    return PSA_SUCCESS;
}
psa_status_t psa_key_derivation_setup(psa_key_derivation_operation_t *op, psa_algorithm_t) {
    op->ready = true;
    return PSA_SUCCESS;
}
psa_status_t psa_key_derivation_input_integer(psa_key_derivation_operation_t *op,
                                              psa_key_derivation_step_t, uint64_t value) {
    op->iterations = value;
    return PSA_SUCCESS;
}
psa_status_t psa_key_derivation_input_bytes(psa_key_derivation_operation_t *op,
                                            psa_key_derivation_step_t step, const uint8_t *data,
                                            size_t size) {
    auto &target = step == PSA_KEY_DERIVATION_INPUT_SALT ? op->salt : op->password;
    target.assign(data, data + size);
    return PSA_SUCCESS;
}
psa_status_t psa_key_derivation_output_bytes(psa_key_derivation_operation_t *op, uint8_t *output,
                                             size_t size) {
    if (psa_mock_fail || !op->ready || size > 32 || op->iterations == 0)
        return PSA_ERROR_GENERIC_ERROR;
    // PBKDF2 with a 32 byte output is one block (index 1).
    uint8_t index[4] = {0, 0, 0, 1};
    auto u = hmac(op->password, op->salt.data(), op->salt.size(), index, 4);
    auto t = u;
    for (uint64_t i = 1; i < op->iterations; ++i) {
        u = hmac(op->password, u.data(), u.size());
        for (int j = 0; j < 32; ++j)
            t[j] ^= u[j];
    }
    std::memcpy(output, t.data(), size);
    return PSA_SUCCESS;
}
psa_status_t psa_key_derivation_abort(psa_key_derivation_operation_t *op) {
    *op = psa_key_derivation_operation_t{};
    return PSA_SUCCESS;
}
