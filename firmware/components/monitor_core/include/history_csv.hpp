#pragma once
#include "monitor.hpp"
namespace monitor {
const char *history_csv_header();
std::string history_csv_row(const Bucket &bucket, const Aggregate &sensor, bool simulated);
} // namespace monitor
