# ESP32-S3 Temperature Monitor — Project Handoff (2026-10-29)

**Status:** Software implementation and CI packaging substantially complete;
physical-device qualification and release approval remain open. This is a
handoff, **not a release approval or evidence of physical acceptance**.

## Repository and reproducible checkpoint

- Repository: [ekkus93/esp32-lcd-temperature-sensor](https://github.com/ekkus93/esp32-lcd-temperature-sensor)
- Target branch: **`master`**. Work directly on master; do not create a PR or
  a separate branch for each task. Use Ralph Bridge exclusively for remote
  GitHub/CI operations, with exact-head compare-and-swap commits.
- **Baseline master SHA at handoff preparation:**
  `8ea8ee0408a04189df76ab677953e5584d301ba6` (`docs(release): prepare T11-08 release-notes draft and record hardware blockers`).
  This new handoff will be committed **after** the baseline; obtain the new
  actual master SHA from GitHub, rather than treating the baseline as the
  handoff's own commit.
- **Baseline exact-SHA CI: PASSED** — [GitHub Actions run 40995561355](https://github.com/ekkus93/esp32-lcd-temperature-sensor/actions/runs/40995561355).
  The final handoff commit requires its own exact-SHA CI check; do not
  extrapolate the baseline result to a newer commit.
- Primary specification: [SPEC.md](SPEC.md). Canonical numbered tracker:
  [TODO.md](TODO.md). Detailed outcomes: [PROGRESS.md](PROGRESS.md).
  Rolling technical notes: [HANDOFF.md](HANDOFF.md). Build instructions:
  [DEVELOPMENT.md](DEVELOPMENT.md).
- Release preparation: [RELEASE_PROVENANCE.md](RELEASE_PROVENANCE.md) and
  [RELEASE_NOTES_DRAFT.md](RELEASE_NOTES_DRAFT.md). CI artifacts are
  **development-only**, explicitly not release qualified.
- SDK: pinned **ESP-IDF v6.1**, revision
  `fff9895c82d744c7237be8847347bdd1b07c6643`; Xtensa GCC
  `esp-15.2.0_20251204`. Target ESP32-S3-N16R8 (16 MiB flash / 8 MiB PSRAM).
  Verify the physical board configuration before treating these as measured.

## Canonical completion

| TODO scope | Complete | Open | Total | Completion |
| --- | ---: | ---: | ---: | ---: |
| Numbered implementation/working-agreement tasks (T00–T11) | **114** | **31** | **145** | **78.62%** |
| Seven definition-of-done acceptance criteria | 0 | 7 | 7 | 0% |
| Optional, explicitly non-v1 backlog | 0 | 6 | 6 | 0% |
| **All literal TODO checkboxes** | **114** | **44** | **158** | **72.15%** |

The 31 open numbered tasks are largely hardware/device acceptance, **not 31
unimplemented software features**. Most have substantial host/CI-level
implementation and tests. Do not check off a task requiring device evidence
based on a simulator, bare-board test without attached peripherals, or a
successful firmware compile. The optional backlog is not part of v1 gating.

### All open numbered tasks grouped by phase

| Phase | Open task IDs | Remaining acceptance |
| --- | --- | --- |
| T02: board and 1-Wire probes (5) | T02-01, T02-02, T02-03, T02-14, T02-15 | Confirm actual board/USB/pinout, LCD/backlight electrical properties, single 4.7 kΩ GPIO4 pull-up, and safe 1–5 real-probe testing and reconnect. |
| T04: display (4) | T04-01, T04-02, T04-03, T04-14 | Validate actual ST7789 rotation/color/offsets, safe backlight supply, PSRAM/DMA and LVGL refresh/heap/legibility and two-mode cycling. |
| T05: Wi-Fi/BOOT (7) | T05-01, T05-04, T05-05, T05-10, T05-11, T05-13, T05-18 | Test real phone AP onboarding, saved-network DHCP/NTP, candidate failure and previous-network retention, physical five-second BOOT recovery and USB download mode. |
| T06: persistent history (3) | T06-14, T06-15, T06-16 | Measure actual retention/bytes and concurrent export, test flash-full/metadata/reboot, and controlled physical LittleFS power interruption/recovery. |
| T07: HTTP/WebSockets (1) | T07-10 | Finish actual ESP32 slow-client/backpressure, session/disconnect/reconnect and concurrent-device-socket acceptance beyond host/POSIX simulations. |
| T09: protected system actions (1) | T09-15 | Qualify guarded restart and two factory-reset modes **only on an isolated disposable target** for destructive cases. |
| T10: OTA and stability (7) | T10-01, T10-02, T10-03, T10-05, T10-08, T10-09, T10-10 | Verify successful OTA A→B→A, web asset/API consistency, forced rollback, combined 5-probe/LCD/Wi-Fi/socket/flash load, 24–72h soak and BOOT offline recovery. |
| T11: final docs/release (3) | T11-01, T11-08, T11-09 | Reconcile measured wiring/behavior against docs, issue final exact-SHA checksummed release and approved notes after all blockers, then reconcile final completion. |

**No open numbered tasks in T00, T01, T03 or T08.** The seven
definition-of-done criteria and six optional backlog checkboxes are separate
from these 31 numbered tasks.

## What exists and what is already verified in software

- **Sensor and application core:** Single shared RMT 1-Wire bus, bounded
  DS18B20 ROM discovery, conversion/CRC/fault handling, ROM-keyed NVS
  configuration, calibration, 5-second live acquisition cadence, status and
  alert transitions. Native simulated zero-to-five-probe scenarios and
  settings corruption/identity regressions exist.
- **Display:** Shared LVGL 9 read-only Overview/Trends views, Basic/Extended
  screen mode, 20 s/10 s default dwell values, simulated rendering of
  disconnected/invalid probes, clock/Wi-Fi status and sparklines. ST7789
  SPI firmware and opt-in RGB/corner test pattern compile. **Do not
  infer physical panel behavior from headless LVGL tests**.
- **History and journal:** Five-minute aggregate buckets, LittleFS
  history segments, CSV/history pagination, CRC validation, retention and
  recovery, event journal and numerous host corruption/append/compaction
  regressions. Recent cross-segment, post-write and early-stop integrity
  tests run in CI; real flash durability, wear/load and power loss remain
  unmeasured.
- **Networking and web:** Local embedded React 19 + TypeScript + Vite UI,
  authenticated HTTP APIs and WebSocket snapshots, SoftAP pairing and
  candidate station workflow. Host simulator covers REST/WebSocket, several
  reconnect/backpressure cases, React pages and schema validation; some
  historical isolated USB-loopback tests exist. Neither simulated sockets
  nor USB loopback prove physical RF/DHCP/phone acceptance.
- **OTA/system:** Authentication/CSRF gates, guarded OTA upload, pending
  startup validation and bootloader rollback policy; separate reboot and
  two reset choices. A successful image update, forced rollback, destructive
  factory reset and end-to-end on-device asset pairing are **not** accepted.
- **Packaging/provenance:** GitHub Actions builds with pinned ESP-IDF v6.1,
  verifies native/Python, simulator, headless LVGL, React and firmware;
  generates bootloader/partition table/OTA selector/application images and
  embedded web bundle, full 16 MiB partition validation, SHA-256 manifest
  and development CI artifact. The draft release notes explicitly leave
  approval/version/owner signoff unassigned. Never relabel a CI artifact
  as a qualified production release.

For fine-grained tests/commit evidence use [PROGRESS.md](PROGRESS.md),
[HANDOFF.md](HANDOFF.md) and [validation/](validation/), not this summary
as a substitute for measured results.

## Next work in dependency order (owner/hardware)

1. **Electrical identification first:** Inspect ESP32-S3-N16R8 board,
   USB connectors and LCD header, measure LCD supply and BL/BLK voltage,
   polarity and current. **Keep GPIO15/backlight disconnected/undriven**
   until T02-02/T04-02 are physically verified. Review regulator budget.
2. **One-wire sensors:** Wire GPIO4 to up to five externally powered
   DS18B20 probes with **one** 4.7 kΩ pull-up to 3V3, common ground and no
   accidental parallel module pull-ups. Check one then five probes, ROM
   addresses, stability/latency and removal/reinsertion. Record read-only
   SENSOR_DIAG counters and sanitized measurements.
3. **LCD:** With electrically safe power, run the optional test pattern,
   verify controller/offsets/rotation/color/legibility, LVGL refresh,
   PSRAM/DMA and Wi-Fi-TX supply stability. Check Basic/Extended cycling.
4. **Owner-network and BOOT:** On an owner-controlled network, test actual
   phone onboarding/scan/manual entry, wrong-password retry, DHCP, SNTP,
   saved credentials and re-provisioning preservation; verify BOOT 5 s
   behavior and ROM download mode. Owner credentials belong exclusively
   in a private local file, **never** in Git, reports, or chat.
5. **Flash/device concurrency:** Measure five-minute retained history
   across actual probes and time, export during writes and reboot, observe
   LittleFS faults. Perform physical power interruption only on a prepared
   expendable device with a safe backup and explicit owner participation.
6. **Isolated destructive and OTA acceptance:** Use recoverable/disposable
   hardware for OTA A→B→A, rollback/failure, partition/history
   preservation and both factory-reset modes. Never reset or flash owner
   devices as part of an unattended loop.
7. **Long soak and release:** Record 24–72h heap/PSRAM/watchdog/clock,
   sampling gaps, network/socket/flash behavior and reset causes with
   five real sensors and LCD. Resolve the open acceptance gates, reconcile
   actual measured wiring/docs, assign release version and exact SHA,
   independently verify downloaded `SHA256SUMS.txt` and obtain explicit
   approval of release notes/known issues.

## Software-only work policy and planned reviews

The owner requested completing the **existing** TODO, followed by
multiple rounds of code review. Do not cycle indefinitely through
speculative storage/journal hardening merely to generate commits.
Fix a concrete unfinished TODO subtask or demonstrated defect when
independently actionable; otherwise record prospective nonblocking
improvements under the optional/future part of [TODO.md](TODO.md) for
the planned review rounds. Keep hardware-dependent boxes open until
their evidence exists. A software-only loop may correctly conclude
that it is blocked on device access; report that explicitly.

## Reproduce, continue, and commit safely

1. Refresh the **live** exact `master` SHA, full
   [TODO.md](TODO.md), [PROGRESS.md](PROGRESS.md),
   [HANDOFF.md](HANDOFF.md), relevant source, and exact-SHA CI.
   Account for concurrent commits and any newer dated handoff.
2. For local host validation, see [DEVELOPMENT.md](DEVELOPMENT.md).
   With managed LVGL dependencies and the pinned SDK available,
   `./tools/verify.sh --firmware` runs the native C++/Python, simulated
   REST/WebSocket, headless LVGL, React checks and ESP-IDF build. Do
   **not** assume an old sandbox or SDK installation still exists.
3. Use **Ralph Bridge only** for repository reads/writes and CI. Commit
   directly to `master` with the exact-head expected SHA; do not create
   per-task branches or PRs. A successful source write alone is not CI
   evidence: require a passing **exact-new-commit** run, inspect and
   reconcile failures before closing the iteration.
4. Record concrete implementation/test evidence and blocked prerequisites
   in [TODO.md](TODO.md) and [PROGRESS.md](PROGRESS.md). Never invent
   probe/LCD/owner-network, physical power loss, OTA/rollback, factory-reset
   or sustained-load results. Never read/print/commit secrets, flash owner
   hardware or operate destructive reset.

**Resume point:** The code and software tests are prepared; the principal
remaining path is hands-on acceptance. Baseline `8ea8ee0408a04189df76ab677953e5584d301ba6` passed
CI 40995561355; **check the new handoff commit's own CI** and report its
actual SHA and result separately.
