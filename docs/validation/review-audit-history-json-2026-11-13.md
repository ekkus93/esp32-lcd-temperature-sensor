# Allocation-safe history JSON (review audit A05, finding F7) — 2026-11-13

Host evidence and a firmware build. Allocation faults are injected on the host only.

## Defect

`HttpApi::serve_history()` did not check the required `gaps` array: when that allocation failed, a
zero-gap answer was sent without `gaps` and without `truncated: true`, so it broke
`contracts/history.schema.json`. `truncated` was updated by looking it up again, which silently did
nothing if it had not been added. Reproduced by `history_json_allocation_faults` with the baseline's
unchecked `gaps` allocation restored: the test fails on the first case (empty, zero gaps).

## Fix

- `http_api/history_json.{hpp,cpp}` (new): `build_history_json()` is the builder the handler uses. It
  checks the root, every required scalar and both arrays, and returns nothing if any of them, or
  serialization, fails. Each point or gap row is built completely before it is attached, or left
  out whole. A left-out row (or more than 512 gaps) sets `truncated` through the pointer kept from
  creating it, which allocates nothing.
- The handler sends the body, or a constant 503 body (`{"error":"History response memory
  unavailable"}`) that needs no allocation. The history worker still releases the admission slot
  after every outcome.

## Tests (`firmware/tests/history_json_tests.cpp`, ctest `history_json_allocation_faults`)

Five cases (empty with zero gaps in UTC and in uptime, points only, points with derived and recorded
gaps, an answer the reducer had already truncated). For each, every allocation is failed in turn,
once and from then on. Every result is either no answer or a body checked against the schema: exact
key sets (read from `contracts/history.schema.json`), types, ID patterns, ranges and array limits. A
body missing any row must be `truncated`. Totals: 16 to 157 allocations per case, 32 to 176 faults
refused, up to 138 valid partial answers. More than 512 gaps gives 512 and `truncated`.

The test compiles the pinned cJSON from `managed_components` (resolved by `idf.py build`, which CI
runs first); without it, CMake warns and leaves the test out.

All 44 host tests and 247 web tests pass; `./tools/verify.sh --firmware` passes.

With this, the production-path seams of A00.2 are all in place: history export (A02), sensor
settings persistence (A03) and history JSON (A05).
