# Honest CSV completion (review audit A02, finding F2) — 2026-11-12

Host evidence and a firmware build. Not run on the board: damaging a stored segment on the device
would alter the owner's history, so the device check of a damaged export is left to a deliberate,
backed-up physical test.

## Defect

`HistoryService::visit()` discarded `HistoryStore::visit()`'s skipped-segment count and returned
`ok && complete`, so `HttpApi::serve_history()` sent the normal final chunk for an export that had
silently passed over a damaged completed segment. Reproduced by `history_export_incomplete` against
the baseline semantics (skip count ignored): the corrupt-first-segment case exported as complete
(`history_export_tests.cpp:159`, `result.end == CsvExportEnd::refused` failed).

## Fix

- `monitor_core/history_visit.{hpp,cpp}` (new, host-built): `visit_history_range()` is the production
  visit loop (range filter, budget, pacing) and returns a `HistoryVisitOutcome`: complete,
  skipped_damaged, deadline, storage_failed, delivery_stopped or unavailable, plus the skip count. A
  store failure outranks a deadline. `stop_on_damage` stops at the first damaged segment.
  `answer_history_query()` is the chart query on the same loop.
- `export_history_csv()` (in `history_csv.cpp`) streams through a `CsvExportSink`. Incomplete before
  the first row: refused (the device answers 503 with the reason). Incomplete after: aborted (the
  device closes the connection without the final chunk). Only a complete export sends the final
  chunk. The CSV schema is unchanged and nothing is appended to announce a failure.
- `HistoryService::visit()` returns the outcome; `query()` uses `answer_history_query()` (a skipped
  segment or the budget still makes the JSON answer `truncated`). The trend restore in `main.cpp` logs
  the outcome.
- `contracts/API.md` says a partial file the browser may hold is not a valid export.

## Tests (`firmware/tests/history_export_tests.cpp`, ctest `history_export_incomplete`)

Real `HistoryStore` in an `mkdtemp` directory, 4 records per segment, 16 records (segments 0–2
completed, 3 active):

| Case | Result |
| --- | --- |
| Healthy | complete, 16 rows, final chunk; same file with 64-byte and 4 KiB chunks |
| No matching record | complete, header row only, final chunk |
| Damaged first completed segment | refused before headers, nothing sent; damaged file kept |
| Damaged middle / last completed segment | aborted, no final chunk, stops at the damage |
| Damage found only at the end of the window | aborted after all 8 rows, no final chunk |
| Damaged active segment | aborted, `storage_failed` |
| Client send fails (mid-stream, at headers, at the last chunk) | aborted, no final chunk |
| Budget expired | `deadline` after 5 records |
| Chart query, same damage | valid, `truncated`; healthy not truncated; damaged active fails |

All 42 host tests, 54 Python tests and 246 web tests pass; `./tools/verify.sh --firmware` passes
(app 2,179,344 bytes). One earlier verify run hit the known `DeviceSettings.test.tsx` flake (finding
F8: `expected '/api/v1/clock' to be '/api/v1/settings'`), unrelated to this change; A08 fixes it.

The seam added here (A00.2, history export) runs the device's own visit and export code; the
`httpd` calls in `HttpCsvSink` are the only part not run on the host.
