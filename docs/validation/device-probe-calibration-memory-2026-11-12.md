# Probe calibration memory on the board — 2026-11-12

Physical check of the calibration copy kept in each DS18B20's own TH/TL bytes (commit 104d2aa;
the ±12.7 °C limit followed in 43c6e7b). The design is in `contracts/API.md`, "Calibration stored
on the probe"; host coverage is the `probe_calibration_memory` ctest entry.

## Setup

- ESP32-S3-N16R8, firmware 0.2.0 flashed over USB without erasing (`ota_0`, `valid`).
- Three real probes on GPIO4: `284d086700000009`, `28d2736f0000009d`, `2802696f00000098`.
  A fourth saved probe, `2861e76c000000e3`, was not connected.
- Every saved calibration offset was 0 °C. Before this firmware the probes held factory TH/TL
  bytes, which the monitor does not treat as its own.

## Results

| Check | Result |
| --- | --- |
| First start on the new firmware | Each connected probe was written and verified by reading back after RECALL E2. Console: `Probe 284d086700000009 calibration memory: stored` at 4.0 s, `28d2736f0000009d` at 8.0 s, `2802696f00000098` at 13.0 s, one probe per cycle. |
| API | `GET /api/v1/sensors` gave `probe_calibration` `stored` for the three probes and `unknown` for the disconnected one. Offsets stayed 0. |
| Restart (RTS reset, 30 s of console captured) | The monitor reached `READY` and logged no probe writes. All three probes read back as `stored`, so a matching probe is not rewritten. |
| After the 43c6e7b flash | Still `stored`/`stored`/`stored`/`unknown`, offsets 0, sensor storage `{"state":"ready","writable":true}`. Checked again about an hour later (uptime 3,702 s) with the same result. |

## Not checked on the board

- **A non-zero offset.** Only 0 °C was written. The encoding of ±0.1 to ±12.7 °C and the
  rewrite after a change are covered by host tests only.
- **Restoring from the probe.** A probe retired and plugged back in, or moved to another monitor,
  has not been shown to adopt its saved offset (`restored`). The owner deferred the
  unplug/retire/replug test.
- **Write failures.** The three-try limit and the `failed` state were not exercised on hardware;
  no clone or faulty probe was available.

These are host-tested behaviours, not hardware acceptance.
