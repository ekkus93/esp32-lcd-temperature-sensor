#pragma once
#include <array>
#include <cstdint>

namespace monitor {
enum class LoginGate { allow, busy, locked };

// Identifies a client by its network address alone (never the source port, which changes with
// every connection). An IPv4-mapped IPv6 address yields the same value as the plain IPv4 address
// (the 32 bits as read from sockaddr_in.sin_addr). Takes the 16 bytes of an IPv6 address.
uint32_t client_identity_ipv6(const uint8_t *address16);
enum class LoginOutcome { verified, wrong_password, kdf_error };

// Decides whether a login attempt may start. The password check is a deliberately slow key
// derivation (about 15 s on the ESP32-S3), so only one runs at a time, and a client that keeps
// guessing backs off on its own: one address cannot lock the owner out on another.
class LoginThrottle {
  public:
    struct Decision {
        LoginGate gate;
        int64_t retry_after_ms; // meaningful unless the gate is `allow`
    };
    // On `allow` a derive is marked in flight until finish() or abort().
    Decision begin(uint32_t client, int64_t now_ms);
    void finish(uint32_t client, LoginOutcome outcome, int64_t now_ms);
    // The attempt never reached the derive (for example the work queue was full).
    void abort(uint32_t client);

  private:
    struct Client {
        uint32_t id = 0;
        bool used = false;
        unsigned failures = 0;
        int64_t next_ms = 0;
        int64_t last_used_ms = 0;
    };
    Client *find(uint32_t client);
    Client *claim(uint32_t client, int64_t now_ms);
    std::array<Client, 8> clients_{};
    bool in_flight_ = false;
};
} // namespace monitor
