# Review audit fixes: final verification and status (A21, gate H13) — 2026-11-13

## Exact-commit evidence (A21.1, A21.2)

- Final code commit: `3c52431ac71322deba85be34162c4cb8109d88e7` (later commits only add records).
- Local, with pinned ESP-IDF v6.1 activated: `./tools/verify.sh --firmware` passed. 47 host tests,
  59 Python tests, the simulator and API checks, headless display, 263 web tests, formatting, lint,
  typecheck, the secret scan, generated contract types matching `contracts/`, and the firmware build
  (application 2,182,064 bytes).
- GitHub Actions run `43399938296` on `3c52431` passed. Runs on `ac8acdb` and `103c178` were cancelled
  when a newer push superseded them. Earlier per-task runs passed on `4a2419d` (A05), `e33beba`
  (A10), `2e1c8b2` (A11) and `ae7f41e` (A12).
- CI artifact `esp32-s3-firmware-3c52431…` provenance manifest: `source_commit` 3c52431, ESP-IDF
  `fff9895c`, firmware and web version 0.2.0, application 2,182,064 bytes, both OTA slots 4,194,304
  bytes, headroom 2,012,240 bytes, `qualification: CI_BUILD_ONLY_NOT_RELEASE_APPROVED`.
- Memory and performance: no new device measurement. Host figures are in the A04 (cancellation
  timing), A05 (allocations) and A17.1 (retention) records.

## Task status (A21.3)

| Task | Status |
| --- | --- |
| A00–A09, A11, A12 | done (records `review-audit-*`, `ui-quality-2026-11-13/`) |
| A10 | A10.1 and A10.3 done; **A10.2 open**: handlers are not run on the host (needs a production-dispatch harness); the gate and the handler order are covered |
| A13, A14, A15, A16, A18, A19 | open: need probes, the LCD, two client devices, phones or other computers, or a 24-hour soak |
| A17 | A17.1 done (host); A17.2 and A17.3 open (board with a populated store) |
| A20 | A20.2 done (build); A20.1 open (offline board and a real phone) |
| A22 | open (see below) |

Completion gates: **G1 not met** only because of A10.2. G2 is not met: physical evidence for H01–H03,
H05–H08, H10 and H12 is missing. G3 is met for the code at `3c52431`. G4 is pending the owner.

## Remaining gates and prerequisites

| Gate | Missing | Prerequisite |
| --- | --- | --- |
| H01 | 30-minute calibrated log, multi-probe bus unplug | probes wired to the board |
| H02 | corrupt-auth USB recovery with data intact, two-client login load | backup of NVS/history, a second client device |
| H03 | LCD setup timing and wrong-Wi-Fi transitions watched on the panel | the LCD connected and watched |
| H04 | handler-level refusal with mocked side effects (A10.2) | a production-dispatch harness |
| H05/H06 | anonymous phone, `.local` on Linux, Windows, macOS/iOS, Android | those clients |
| H07 | board retention and rollover, short and full-window latency after A02/A04 | probes, a populated store |
| H08 | soak over 24 hours with LVGL, flash-write and Wi-Fi counters | instrumentation (A18.1) and the board |
| H10 | real ice-bath calibration including the extended-open flow | a probe and an ice bath |
| H12 | all pages offline from the board on a phone | board without internet, a phone |

## Owner decisions to record

1. A03 kept schema v1: a failed sensor-settings write shows `read_only` for a 30 s to 10 min
   pause, then edits resume. Accept, or ask for a schema change.
2. R10-03's "well under one second" history objective: broad queries measured 1.43 to 1.93 s on
   2026-11-12. Either measure again after A04, or revise the objective.
3. Rolling back to a build older than `3c1a01d` after calibrating makes the event journal
   unreadable until roll-forward (A11). Accept as a known limit for development builds.
4. Whether to release 0.2.0 with the open gates listed as known issues (A22.2), and whether to
   publish (A22.3).
