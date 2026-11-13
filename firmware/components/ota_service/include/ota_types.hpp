#pragma once
// The running image as plain values, for the HTTP API ports.
#include <string>
namespace monitor {
struct OtaStatus {
    std::string version;
    std::string partition;
    std::string state;
};
} // namespace monitor
