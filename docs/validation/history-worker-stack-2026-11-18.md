# History worker stack overflow, and the board check (G19.2) — 2026-11-18

Board evidence: the ESP32-S3 with three DS18B20 probes reading (a fourth saved, disconnected), ST7735
LCD, on Wi-Fi as `temperature-monitor.local` (100.64.64.61). Flashed over USB without erasing.

## Defect

With the refactored firmware (`ebe8b0c`), a 30-minute soak (`tools/device_soak_profile.py`: two
WebSocket viewers, status every 2 s, history every 5 s, alerts every 10 s, a CSV export every 60 s)
crashed once about three minutes in: `vApplicationStackOverflowHook` while the task was in
`visit_history_range()`. Static stack reports (`-fstack-usage`) showed
`segment_fingerprint()` in `history.cpp` with a 4,400-byte frame: a 4 KiB `std::array` read buffer.
It runs when a history visit revalidates its segments, at the bottom of a chain on the 8 KiB
`history_http` worker: worker, `serve_history`, `export_history_csv`, `HistoryPort::visit`,
`HistoryService::visit`, `visit_history_range`, `HistoryStore::visit` (784 bytes),
`unchanged_segment_contents`, `segment_fingerprint`, then the file system's read path. The buffer is
old; the review-audit history changes (A02/A04) and the God file refactor each added call layers,
enough to overflow under some interleavings. Back-to-back exports alone did not reproduce it.

## Fix

- `segment_fingerprint()` reads into a heap buffer from `external_malloc()` (PSRAM when present,
  like the segment snapshot); a failed allocation fails the visit instead of aborting.
- The history worker logs `stack_min` (its lowest free stack) with each request.
- `tests/check_history_stack.py` (ctest `history_stack_frames`): the host build compiles
  monitor_core with `-fstack-usage`; no function in the history read path may have a frame over
  2 KiB (host frames run about 1.5 times the device's). It fails on the old code, naming
  `segment_fingerprint` (4,752 bytes on the host).

## Board results

| | Old firmware (12 Nov, before the audit fixes) | Refactor, before the fix | Refactor with the fix |
| --- | --- | --- | --- |
| `device_api_test.py --expect-sensors 4` | 506 checks passed | 506 passed (same set) | not re-run (fix touches no route) |
| Soak crashes / restarts | 0 | 1 stack overflow | 0 |
| Soak requests (status, history, alerts, export) | all 200 | 5 timeouts around the crash | all 200, same counts as the old firmware |
| `history_http` lowest free stack | not logged | overflowed | 4,060 bytes of 8,192 |
| Main task lowest free stack | 2,284 | 3,068 | 3,068 |
| Display frames dropped | 0 | 0 | 0 |
| Internal RAM lowest free | 10,120 | 228 | 5,536 |
| Sign-in time | 3.2–3.4 s | 3.2–3.3 s | — |

Two shorter load runs on the refactor (9 and 12 minutes) bottomed at 20,612 and 20,076 bytes of
internal RAM. The dips are single moments (steady free internal RAM is 42–55 KB on both
firmwares), and their size varies a lot between runs. Their cause is not yet known.

## Status

The crash is fixed. G19.2 is not ticked: the lowest free internal RAM is not the same as the old
build's, and a 228-byte dip is a risk of its own that needs its own investigation.
