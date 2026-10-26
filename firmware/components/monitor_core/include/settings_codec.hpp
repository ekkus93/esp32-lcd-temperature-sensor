#pragma once
#include "monitor.hpp"
namespace monitor {
struct SavedSensor {
    uint64_t rom;
    Settings settings;
};
std::string encode_settings(const std::vector<Sensor> &sensors);
std::optional<std::vector<SavedSensor>> decode_settings(const std::string &bytes);
} // namespace monitor
