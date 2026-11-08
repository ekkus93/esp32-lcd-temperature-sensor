#pragma once
// Just enough of the PSA API for AuthService: a real PBKDF2-HMAC-SHA256 so the known-answer
// self-test and real verifier comparisons run, and random bytes from the OS.
#include <cstddef>
#include <cstdint>
#include <vector>
using psa_status_t = int;
constexpr psa_status_t PSA_SUCCESS = 0;
constexpr psa_status_t PSA_ERROR_GENERIC_ERROR = -132;
using psa_algorithm_t = uint32_t;
constexpr psa_algorithm_t PSA_ALG_SHA_256 = 0x02000009;
constexpr psa_algorithm_t PSA_ALG_PBKDF2_HMAC(psa_algorithm_t) {
    return 0x08800100;
}
enum psa_key_derivation_step_t {
    PSA_KEY_DERIVATION_INPUT_COST,
    PSA_KEY_DERIVATION_INPUT_SALT,
    PSA_KEY_DERIVATION_INPUT_PASSWORD
};
struct psa_key_derivation_operation_t {
    bool ready = false;
    uint64_t iterations = 0;
    std::vector<uint8_t> salt, password;
};
#define PSA_KEY_DERIVATION_OPERATION_INIT                                                          \
    {                                                                                              \
    }
// Test hook: make key derivation fail (a broken crypto engine).
extern bool psa_mock_fail;
psa_status_t psa_crypto_init();
psa_status_t psa_generate_random(uint8_t *output, size_t size);
psa_status_t psa_key_derivation_setup(psa_key_derivation_operation_t *op, psa_algorithm_t alg);
psa_status_t psa_key_derivation_input_integer(psa_key_derivation_operation_t *op,
                                              psa_key_derivation_step_t step, uint64_t value);
psa_status_t psa_key_derivation_input_bytes(psa_key_derivation_operation_t *op,
                                            psa_key_derivation_step_t step, const uint8_t *data,
                                            size_t size);
psa_status_t psa_key_derivation_output_bytes(psa_key_derivation_operation_t *op, uint8_t *output,
                                             size_t size);
psa_status_t psa_key_derivation_abort(psa_key_derivation_operation_t *op);
