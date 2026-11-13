#include "history_visit.hpp"
namespace monitor {
HistoryVisitOutcome visit_history_range(const HistoryStore &store, const HistoryRange &range,
                                        const std::function<bool(const Bucket &)> &visitor,
                                        const HistoryVisitOptions &options) {
    std::size_t skipped = 0;
    unsigned visited = 0;
    bool damaged_stop = false, visitor_stopped = false;
    // The budget is enforced inside the store, so it also bounds reading, hashing and validation
    // that deliver nothing.
    HistoryVisitControl control;
    control.expired = options.expired;
    const bool ok = store.visit(
        [&](const Bucket &bucket) {
            if (options.pace && ++visited % 32 == 0)
                options.pace();
            // The store counts a damaged segment before delivering the records after it.
            if (options.stop_on_damage && skipped > 0) {
                damaged_stop = true;
                return false;
            }
            if (!history_bucket_in_range(bucket, range))
                return true;
            if (!visitor(bucket)) {
                visitor_stopped = true;
                return false;
            }
            return true;
        },
        &skipped, &range, &control);
    HistoryVisitOutcome outcome;
    outcome.skipped_segments = skipped;
    outcome.files_read = control.files_read;
    // A cancelled visit stopped where it was. Otherwise the store validated every segment even
    // after delivery stopped, so its failure outranks the rest.
    if (control.cancelled)
        outcome.end = HistoryVisitEnd::deadline;
    else if (!ok)
        outcome.end = HistoryVisitEnd::storage_failed;
    else if (visitor_stopped)
        outcome.end = HistoryVisitEnd::delivery_stopped;
    else if (damaged_stop || skipped > 0)
        outcome.end = HistoryVisitEnd::skipped_damaged;
    else
        outcome.end = HistoryVisitEnd::complete;
    return outcome;
}
HistoryQueryAnswer answer_history_query(const HistoryStore &store, const HistoryQuery &query,
                                        const HistoryVisitOptions &options) {
    HistoryReducer reducer(query);
    HistoryQueryAnswer answer;
    answer.visit = visit_history_range(
        store, {query.boot_id, query.from, query.to},
        [&](const Bucket &bucket) {
            reducer.add(bucket);
            return true;
        },
        options);
    if (answer.ok()) {
        answer.points = reducer.finish();
        answer.gaps = reducer.gaps();
        answer.truncated = reducer.truncated() || !answer.visit.complete();
    }
    return answer;
}
} // namespace monitor
