# Cooperative history cancellation (review audit A04, finding F4) — 2026-11-12

Host evidence and a firmware build. Device latency (H07) is not measured here and stays open.

## Defect

A visitor returning false stopped delivery but the store kept reading, hashing and validating every
remaining segment, so the three-second query and fifteen-second export budgets did not stop the scan
or release the history mutex.

## Fix

- `HistoryVisitControl` (`history.hpp`): an `expired` check that is separate from pagination. It is
  checked before each segment, after every read or hash chunk of at most 4 KiB, before each record
  (validation and delivery) and before and during the final revalidation. Once expired, the visit
  starts no further segment, skips the revalidation and returns false with `cancelled` set, so it is
  never reported as validated. `files_read` counts segment files opened.
- `visit_history_range()` passes the service budget into the store and reports `deadline`. JSON
  answers stay valid and `truncated`; CSV follows A02 (refused or aborted).
- Normal completion keeps every integrity check (snapshot, size, fingerprint, host file identity).
- The service measures each budget from when it holds the mutex and logs lock wait and run time;
  the HTTP worker logs total time. The admission slot is released after every outcome (unchanged).

## Residual bounds (not hard real-time)

A deadline cannot interrupt a blocking primitive already running: one 4 KiB `read`, one record
decode, and, for an export, one `httpd_resp_send_chunk` of about 4 KiB (send timeout 3 s). Cleanup
after cancellation is the mutex release and response close. The worst case past the budget is
therefore about one chunk read plus one socket send timeout.

## Tests

- `history_cancel_deadline` (new): a 6-segment store (~7 KiB segments) with a counting fake clock.
  Cancelling at about 100 points across the whole visit, including the first and last checks: always
  false, flagged, no file opened after the deadline, no extra check. Deadline during the first hash:
  one file, nothing delivered. Pagination still validates every segment, and the deadline bounds that
  validation. Out-of-range segments are not read. Corruption followed by a deadline. The service
  outcome is `deadline` and the chart answer is valid and truncated.
- Mutation checks: removing the before-segment check, or ignoring the deadline inside the store,
  both fail the test.
- Host timing: a full visit of the 6-segment store took 9.6 ms (1,048 deadline checks).
- All 43 host tests and 247 web tests pass; `./tools/verify.sh --firmware` passes.

## Still open

Writer contention against the FreeRTOS mutex and the real full-store latency are device-only. The
original "well under one second" objective (R10-03) is not met by the recorded 1.43–1.93 s broad
queries and is not reinterpreted: H07 stays open until measured on a full store, or the owner
approves a revised objective.
