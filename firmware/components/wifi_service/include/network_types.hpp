#pragma once
// Wi-Fi state and scan results as plain values (no ESP-IDF types), for the HTTP API ports.
#include <cstdint>
#include <string>
namespace monitor {
struct NetworkInfo {
    std::string state;
    std::string ssid;
    std::string ip;
    bool provisioning;
    std::string attempt;
    unsigned remaining_seconds;
    bool saved_credentials;
    std::string error;
    // Uptime ms when `state` last changed.
    int64_t state_since_ms = 0;
};
struct ScannedNetwork {
    std::string ssid;
    int rssi;
    unsigned channel;
    bool secure;
};
} // namespace monitor
