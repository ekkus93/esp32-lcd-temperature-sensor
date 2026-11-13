# Sensor settings retry after a transient failure (review audit A03, finding F3) — 2026-11-12

Host and browser-test evidence and a firmware build. NVS faults are not injected on the board.

## Defect

`SettingsService` stayed writable after an open, set or commit failure, but both save paths in
`firmware/main/main.cpp` (the owner-edit commit and the automatic save) set the API's
sensor-settings flag to false for good, so one transient NVS error made sensor settings read-only
until restart. The baseline had no seam around that decision; the regression is the new
`SensorSettingsPersistence` test (it requires edits to be accepted again after the pause, which the
permanent flag never allows) and the browser test's recovery half (the page stayed read-only).

## Compatibility decision

Schema v1 is kept. `GET /sensors/storage` has only `ready`/`read_only`, and the browser learns an
edit was not committed from an unchanged revision plus `read_only`. Keeping the status `ready`
after a failure would make a failed edit look "still pending", so a failed write now makes it
`read_only` for a bounded pause (30 s, doubling up to 10 min, cleared by a success), after which it
returns to `ready` on its own. Damaged or unsupported stored settings stay `read_only` for the whole
boot, as before. No wire, storage or schema change; `contracts/API.md` describes the rule.

## Fix

- `settings_service/sensor_settings_persistence.{hpp,cpp}` (new, host-built): `commit_edits()`
  writes owner edits and on failure restores the committed registry (revision included) and starts
  the pause; `persist()` writes automatic changes (a discovered probe) once the pause is over, never
  every cycle; `accepting_edits()` is what the API advertises: the service's own writability and no
  pause.
- `main.cpp` uses it for both paths and publishes `accepting_edits(now)` each cycle, before the
  registry, so the browser sees `read_only` together with the rolled-back revision. Acquisition,
  history and the display carry on.
- `SensorsView.tsx` rechecks the storage status every 15 s while it reads `read_only` and re-enables
  editing (and reloads the sensors) when it is `ready` again.

## Tests

- `sensor_settings_store_failures` (`settings_service_tests.cpp`), for open, set and commit failures:
  rollback of name and revision, nothing written, edits refused until exactly 30 s later, sampling
  continues, the second edit is stored and survives a reload. Backoff 30 s → 10 min cap and reset
  after success. A failed automatic save makes no NVS call during the pause and is written after it.
  Corrupt stored data: never accepting edits, never written, edits rolled back. The existing
  corrupt/future/unreadable-load preservation cases pass unchanged.
- `SensorsView.test.tsx`: a failed commit shows "Settings were not committed…" (never "committed"),
  the form is disabled, and once the device reports `ready` the page re-enables on its own and a
  second edit is committed.

All 42 host tests and 247 web tests pass; `./tools/verify.sh --firmware` passes.
