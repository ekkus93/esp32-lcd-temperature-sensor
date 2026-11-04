# ESP32-S3 Temperature Monitor Handoff — 2026-10-31

**Status:** In progress; release and hardware qualification are not complete.

## Current state

The canonical backlog is [TODO.md](TODO.md), with implementation evidence in
[PROGRESS.md](PROGRESS.md). There are **124 complete and 21 open of 145
numbered tasks**. Open IDs: T02-01/02/03/14/15; T04-01/02/03/14;
T05-10/13/18; T06-15/16; T10-02/05/08/09/10; T11-01/08.

The ESP32-S3 at `/dev/ttyACM0` runs the latest normal ESP-IDF v6.1 image
(`0x1ee5f0` bytes). It passed 15-second and 120-second bare-board smokes: READY,
healthy history/event storage, zero physical sensors, and no panic. Twenty-four
complete health samples in the 120-second run showed stable heap and stack
watermarks. A dedicated 100 ms task owns
LVGL updates; it expires only its mutex-protected registry snapshot and can
publish stale state to HTTP/WebSocket without mutating the acquisition-owned
registry. Physical screen behavior is not verified.

The 2026-10-31 full verifier passed 24 host CTests, 14 Python tests, simulator
API/load checks, headless LVGL, 78 web tests, formatting, lint, typecheck, Vite
build and the pinned ESP-IDF v6.1 build. Focused storage tests passed 4/4. The
production `HistoryStore` profile measured POSIX host segment eviction for 1–5
probes; MCU LittleFS retained-range evidence remains open.
Detailed latest checks are in [display-refresh validation](validation/device-display-refresh-2026-10-31.txt),
[API stress validation](validation/device-api-stress-2026-10-31.txt),
[KDF validation](validation/device-kdf-worker-2026-10-31.txt), and
[replacement validation](validation/device-sensor-retirement-2026-10-31.txt),
[bare-board stability validation](validation/device-bare-board-2026-10-31.txt),
and [history retention profile](validation/history-retention-estimate.md).

The software pin map was cross-checked against `SPEC.md`, the wiring table and
diagram, and the LCD design spec: GPIO4 for 1-Wire; GPIO12/11/10/9/14 for LCD
SCLK/MOSI/CS/DC/RST; GPIO15 conditional for backlight; GPIO0 for BOOT. This
does not establish physical board orientation, LCD connector order, controller,
supply, or backlight electrical behavior. T11-01 remains open for those
measurements and final reconciliation.

## Remaining prerequisites

- LCD is disconnected. T02-02/03, T04-01/02/03/14 and visible portions of
  T05-10/T10-02 need panel identification, safe wiring, and physical checks.
- Temperature probes are disconnected. T02-14/15, T06-15, T10-08, and
  populated-history portions of T10-09 require probes and the approved bus
  wiring. Do not wire GPIO15 until the LCD/backlight electrical check passes.
- T02-01 needs board markings/second USB/button routing identification; a clear
  photo and physical inspection remain unavailable.
- T05-13/18 and T10-10 need BOOT/provisioning, phone/network and offline
  recovery acceptance. Existing USB/AP tests do not substitute for these.
- T06-16 needs controlled power interruption. T10-08 needs 24–72 hours of
  combined operation. T10-05 still needs station-network and combined
  sensor/LCD/flash traffic profiling. T10-02 needs actual component failure
  injection and eventual LCD status visibility.
- T11-01 needs final measured hardware reconciliation. Keep release packaging
  T11-08 blocked until qualification and owner signoff.

The device test-admin login is currently un-enrolled after the USB helper
reset only its test-managed credential keys. Re-enrollment is waiting for
provisioning mode; the saved Wi-Fi/history partitions were not targeted. The
next device API/load suite must wait until the test account is restored.

The working tree is uncommitted. Credentials remain in ignored `.build/` files
and must not be copied into reports or version control.
