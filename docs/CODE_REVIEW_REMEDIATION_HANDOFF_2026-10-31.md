# Code Review Remediation Handoff — 2026-10-31

**Status:** In progress. Software verification and selected MCU checks pass; release qualification is incomplete.

## Workspace and verified state

Continue with [CODE_REVIEW_REMEDIATION_TODO.md](CODE_REVIEW_REMEDIATION_TODO.md), using [CODE_REVIEW_REMEDIATION_SPEC.md](CODE_REVIEW_REMEDIATION_SPEC.md) for acceptance criteria. The working tree contains uncommitted remediation changes. The checklist has 52 of 73 numbered entries checked; this count includes parent items and is not a release-completion percentage. The original backlog has 124 of 145 numbered tasks checked; 21 remain open. T06-14 has now passed host fault/power-cut recovery and reboot simulations; physical power interruption remains T06-16.

On 2026-10-31, after the independent 100 ms display-refresh task, settings-failure regression and Alerts unknown-time coverage changes, `source /home/phil/esp/esp-idf-v6.1/export.sh && ./tools/verify.sh --firmware` passed: 24 host CTests, 14 Python tests, simulator/API and load checks, headless display, 78 web tests, formatting, lint, typecheck, Vite build, and pinned ESP-IDF v6.1 firmware build. The image was `0x1ee5f0` bytes with 52% OTA slot headroom. `git diff --check` passed after the build and documentation updates.

The latest normal image (`0x1ee5f0` bytes) was flashed to the ESP32-S3 and returned to READY. Five successive 15-second bare-board smokes after the display/BOOT/freshness changes passed with healthy history, no fabricated probes and no panic; see [display-refresh smoke validation](validation/device-display-refresh-2026-10-31.txt) for resource measurements. Bare-board smoke, isolated simulated five-slot sensor retirement/replacement with authenticated reboot persistence, and the API/WebSocket suite passed. The 120-second stalled-WebSocket run (before the latest display and BOOT-service changes) had 120/120 successful USB-loopback HTTP checks (256 ms maximum), server cutoff, 31 health samples, and no watchdog/backtrace diagnostics. PBKDF2 login completed in about 30.5 seconds while health samples continued without watchdog warnings. These results are recorded in [retirement validation](validation/device-sensor-retirement-2026-10-31.txt), [API stress validation](validation/device-api-stress-2026-10-31.txt), and [KDF worker validation](validation/device-kdf-worker-2026-10-31.txt).

The latest API-suite retry found the private test-admin password stale. The test helper reset only its test-managed administrator; enrollment is now false and waits for physical provisioning mode. A request for a five-second BOOT hold is pending. The board still boots the normal firmware and bare-board smoke passes.

The board repeated the 15-second smoke after USB reconnection on 2026-10-31.
Host regressions now prove sampling continues after corrupt, unsupported,
unreadable and open-failure settings loads; Alerts tests also cover boot-relative
timestamps when synchronization and timezone configuration are unavailable.
`./tools/verify.sh --host`, the focused Alerts test, formatting and
`git diff --check` passed after these additions. The refreshed device evidence
is in [display-refresh validation](validation/device-display-refresh-2026-10-31.txt).

A fresh 120-second bare-board smoke also passed on the connected ESP32-S3.
Twenty-four complete health records showed stable heap/stack metrics, healthy
history and event storage, zero sensors with the bus disabled, and no panic.
This is an idle no-peripheral check, not the T10-08 soak or loaded-network test;
details are in [bare-board stability validation](validation/device-bare-board-2026-10-31.txt).

The corrected MCU history profiler now covers the simulator's accelerated
uptime in its history/export query window and measures storage usage alongside
concurrent status traffic. Its two host time-window regressions pass. An actual
profile still needs authenticated enrollment and a station route from the host.

The LCD and temperature probes are disconnected. No physical display, real-probe, or combined peripheral test is claimed. The board’s bare-board checks and simulation-provider results do not qualify those paths.

## Remaining work

- CR02.5, CR04.6, CR09.6, CR12.6 and the original T02/T04/T10 hardware acceptance items need verified board/wiring/peripherals; do not close them from simulation.
- CR03.4–.6 still need production API failure-boundary coverage, concurrent/recovery cases, and isolated on-device NVS failure injection.
- CR04.5 still needs concurrent mutation/retirement coverage and on-device persistence-failure injection.
- CR06.5 needs a combined slow-body, WebSocket and health cadence run; the workstation could not reach the board’s station IP. Separate slow-body, KDF, and 120-second USB-loopback stress evidence exists.
- CR09.2 and .5 remain: verify panel-visible progress/dwell bounds and close runtime/headless boundary coverage. Physical visible latency remains blocked on the LCD; host tests cover exact stale-expiry threshold and preserve the acquisition-owned registry.
- CR10.3 device integration checks remain; CR10.1 testable request/query/storage policies are separated into production helpers.
- CR12.1, .3 and .5 require closing coverage gaps, populated-storage profiling, and a final findings-to-diff review. The production `HistoryStore` now has a reproducible opt-in POSIX profile for one-to-five-probe record sizes and eviction; it does not cover MCU query/export latency, memory or acquisition continuity.
- Keep original release item T11-08 unchecked until all qualification and signoff gates pass.

The user-authorized test board is `/dev/ttyACM0` when connected. Credentials/setup JSON stay in ignored `.build/` files and must never be copied into reports or version control.
