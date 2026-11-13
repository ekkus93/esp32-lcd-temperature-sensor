#pragma once
#include "history.hpp"
#include "history_query.hpp"
#include <cstddef>
#include <functional>
namespace monitor {
// How a history visit ended (review audit F2). Only `complete` means every stored record in the
// window was offered to the visitor; the others must not be presented as a complete answer.
enum class HistoryVisitEnd : uint8_t {
    complete,
    skipped_damaged,  // finished, but damaged completed segments were passed over
    deadline,         // stopped at the caller's time budget
    storage_failed,   // the store failed: a damaged active segment, I/O, a changed file
    delivery_stopped, // the visitor stopped it, for example because the client went away
    unavailable,      // never ran: history not started, busy or the query invalid
};
struct HistoryVisitOutcome {
    HistoryVisitEnd end = HistoryVisitEnd::unavailable;
    std::size_t skipped_segments = 0; // damaged completed segments passed over
    std::size_t files_read = 0;       // segment files opened (instrumentation)
    bool complete() const {
        return end == HistoryVisitEnd::complete;
    }
};
struct HistoryVisitOptions {
    // True once the caller's time budget is spent. Checked inside the store before each segment,
    // every 4 KiB read or hashed and every record (see HistoryVisitControl).
    std::function<bool()> expired;
    // Called every 32 records so a long visit lets other tasks run.
    std::function<void()> pace;
    // Stop as soon as a damaged segment is passed over: for answers that cannot be marked
    // truncated, such as a CSV file, which is then incomplete whatever follows.
    bool stop_on_damage = false;
};
// The production visit loop (HistoryService runs it under the history mutex): records outside
// `range` are filtered out, the budget and pacing applied, and the store's skip count and failures
// kept in the outcome instead of being folded into one boolean.
HistoryVisitOutcome visit_history_range(const HistoryStore &store, const HistoryRange &range,
                                        const std::function<bool(const Bucket &)> &visitor,
                                        const HistoryVisitOptions &options = {});

// A chart query's answer. Unlike a CSV file it can be partial: damaged segments and the budget
// make it `truncated`, and only a store failure (or never running) fails it.
struct HistoryQueryAnswer {
    HistoryVisitOutcome visit;
    std::vector<HistoryPoint> points;
    std::vector<HistoryGap> gaps;
    bool truncated = false;
    bool ok() const {
        return visit.end != HistoryVisitEnd::storage_failed &&
               visit.end != HistoryVisitEnd::unavailable;
    }
};
HistoryQueryAnswer answer_history_query(const HistoryStore &store, const HistoryQuery &query,
                                        const HistoryVisitOptions &options = {});
} // namespace monitor
