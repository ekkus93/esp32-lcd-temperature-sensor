#pragma once
#include "history_query.hpp"
#include <vector>
namespace monitor {
// The serialized body of a GET /history answer (review audit F7), or nullptr when a body that
// satisfies contracts/history.schema.json cannot be built: the root, every required field and both
// arrays are checked. A point or gap row that cannot be built completely is left out whole and the
// answer is marked `truncated`; nothing half-built is attached. Free the body with cJSON_free().
// The HTTP handler sends it as is; on nullptr it answers 503 with a static body.
char *build_history_json(const std::vector<HistoryPoint> &points,
                         std::vector<HistoryGap> gap_records, bool truncated, bool simulated,
                         bool uptime_basis);
} // namespace monitor
