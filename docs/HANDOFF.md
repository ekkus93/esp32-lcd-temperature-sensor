# Project handoff — 2026-10-27

## Current state

The required backlog is [TODO.md](TODO.md); 98 named items are checked. This is
an active development build, not a release. Firmware targets an ESP32-S3 with ESP-IDF v6.1; the web app is
React, TypeScript and Vite. Core acquisition, protected local HTTP, Wi-Fi setup,
settings, a simulated five-probe provider, bounded temperature history, and a
headless LVGL renderer are implemented. The physical LCD and sensors have not
been connected or validated. Keep their outputs disabled; review GPIO15 and the
electrical design before connecting them.

The latest work adds `TMH2` five-minute records with per-ROM label and state
metadata, legacy `TMH1` read support, and authenticated streaming CSV export.
The History page offers a five-minute CSV download. `/api/v1` response schemas
and generated TypeScript types now cover snapshot, sensor, history, alerts,
settings, auth state and Wi-Fi responses. See
[HISTORY_FORMAT.md](HISTORY_FORMAT.md) for formats and retention estimates.
Earlier simulated ESP32 checks passed CSV authentication, unknown-clock and
simulation markers, recorded labels, flags, and query rejection; see
[device-history-csv-results.txt](validation/device-history-csv-results.txt).
Those are simulated readings on real hardware, not physical-probe results.

At the 2026-10-26 handoff, the ESP32 was unavailable and the flashed image was
unknown. On 2026-10-27, the same board reappeared at `/dev/ttyACM0`. The normal
image was flashed and passed bare-board health and authenticated HTTP/API tests,
including a radio scan; it reports zero sensors and keeps the 1-Wire/LCD disabled.
These checks prove firmware operation without peripherals, not physical sensing.

The current image also includes pinned Espressif mDNS 1.14.1 and advertises
`http://temperature-monitor.local`; normal boot, health smoke and the protected
device API suite passed. Use `http://192.168.4.1` on the setup AP or the DHCP
address when multicast DNS is blocked. Hostname resolution from a joined client
is not yet verified. See [mDNS device results](validation/device-mdns-results.txt).

Alert reads support authenticated cursor pagination (1–50 events per page)
and expose the oldest retained sequence. The event journal is stored in
LittleFS as CRC-protected records, bounded to 128 KiB with compaction and
partial-tail recovery. Host recovery/paging/compaction tests and the pinned
firmware build pass. The device API test now checks journal health, a boot
record, and end-of-log cursor behavior; it has not been run because the board
is currently unavailable. Power-interruption and device persistence checks
remain open under T06-14/16.

The local test setup/admin secrets were scanned against 200 reviewable files
and the current build/device logs without a match; web source has no browser
storage or console logging. Local HTTP still has the LAN confidentiality limit
accepted by the owner.

History JSON now returns explicit gap intervals between same-ROM, same-boot
points, with `partial` retained for gaps inside downsampled groups. The host
reducer and bare-board empty-history API checks passed; details are in
[history gap results](validation/device-history-gap-results.txt).

## Reproduce and resume

The pinned SDK is `/home/phil/esp/esp-idf-v6.1`, revision
`fff9895c82d744c7237be8847347bdd1b07c6643` with GCC
`esp-15.2.0_20251204`. Run:

```sh
source /home/phil/esp/esp-idf-v6.1/export.sh
tools/verify.sh --firmware
```

This runs host core/storage/CSV tests, headless LCD tests, formatting, web lint,
unit tests, typecheck/build, SDK pin verification and firmware build. Use
`docs/DEVELOPMENT.md` for separate simulation and device-test commands. When the
board is connected, identify its serial port before flashing. Restore the normal
image from `firmware/build` after any simulation tests, then run
`tools/device_smoke.py` and `tools/device_api_test.py`. Do not confuse
`firmware/build-sim` with the normal build. Never print or commit ignored
`.build/` administrator or Wi-Fi secrets.

## Remaining work

- `T06-14` through `T06-16`: retention and recovery measurements, simultaneous
  export and reboot checks, and controlled power-interruption verification.
  `T06-03` storage-failure handling and event-journal device/power-loss checks
  remain.
- Historical ROM selection, retired-probe series, bounded pan/zoom, and the
  paginated alert UI are implemented (`T07-05/06`, `T09-05/06/10`). Durable
  alert events across reboot are implemented (`T06-10`); hardware persistence
  verification remains open. History responses expose
  explicit gaps between persisted points; gaps within downsampled groups use
  `partial`.
- Wi-Fi association, DHCP success, NTP, real phone onboarding and BOOT recovery
  need owner network credentials. The owner has not supplied a credential-file
  path. Request an absolute path to a private JSON file with `ssid` and
  `password`; never include its contents in reports or Git.
- OTA upload, Settings upload UI and boot-health rollback logic are implemented;
  device acceptance, rollback and recovery checks remain open (`T10-01` through
  `T10-04`). Physical sensor/LCD integration, fault injection and release gates
  remain open in [TODO.md](TODO.md).

The owner selected local HTTP and accepted its documented LAN confidentiality
limitation. The Ralph loop resumed on 2026-10-27. Continue the required TODO
backlog; no physical LCD or sensor was connected, and owner Wi-Fi credentials
remain unavailable for successful station, NTP, and phone-onboarding checks.

## Latest verification checkpoint — 2026-10-27

The `ws_live` ESP-IDF component now owns WebSocket client tracking, read-only
frame handling and coalesced broadcasts. `http_api` retains origin/session
authorization and provides complete snapshots via callback. The simulator live
integration and full `tools/verify.sh --firmware` pass: 8 host CTests, 1
headless display test, 49 web tests, lint, formatting, typecheck, Vite build and
ESP-IDF v6.1 firmware build. Image size is 0x17bd00 bytes with 63% OTA slot
headroom. No board was available for live WebSocket, slow-client or hardware
tests. At that checkpoint, `T01-02` still had the `sensor_registry`,
`sampling_service` and `alert_service` boundaries open; checked TODO count was
96/145. The simulator integration also passed 30 sequential WebSocket
reconnects. `T10-07` remains open for burst-alert/replacement and true
storage-exhaustion scenarios; existing host suites already cover 24-hour
aggregation, rollover/corruption, clock alignment/steps and settings blob
corruption. Since then, `sensor_registry`, `sampling_service` and
`alert_service` were extracted as components with shared `monitor_types`; the
alert service now owns hysteresis, persistence and state transitions. The
pinned full verifier passed (firmware image 0x17bdf0 bytes, 63% OTA slot
headroom). `T01-02` is checked. The ESP32 was unavailable for hardware tests.
The latest host run adds a 200-cycle alert burst check through `Registry`; it
confirms the event journal is bounded to 128 entries with monotonic cursors. It
also covers one-for-one sensor replacement without identity/configuration
cross-contamination. `T10-07` still needs full five-probe replacement and true
storage-exhaustion scenarios. The live simulator now also verifies that four clients are accepted
concurrently and a fifth is rejected. `T07-10` remains open for slow-client and
fuzz coverage.
Host settings tests now verify two distinct ROM configurations survive reverse
discovery order; `device_sensor_test.py` was expanded to verify both settings
through an isolated simulation-image reboot. That device run remains pending.

Overview and Trends dwell settings are persisted and can be changed without
reboot; the headless renderer uses the 20/10-second defaults and monotonic
timing. Physical display timing remains deferred with the LCD disconnected.

Host tests verify five-minute UTC alignment, unsynchronized boot scope, reboot
isolation and partial-bucket closure after an NTP time step. Actual SNTP
synchronization still requires a configured station network.

Sensor settings also passed an isolated simulated-device NVS mutation and
restart-persistence check; the normal image was restored and passed a 15-second
bare-board smoke. See [sensor NVS results](validation/device-sensor-nvs-results.txt).

Dashboard cards include each probe’s last-reading age; the shared status row
shows Wi-Fi, clock and history health. Empty-probe behavior remains explicit.

Settings covers Wi-Fi changes, device timezone/NTP and units, plus logging
health, five-minute policy and LittleFS capacity diagnostics. Snapshot schemas
include storage availability and total/used bytes, plus firmware version, slot
and OTA state. Settings displays these diagnostics. Forty-nine web tests and
the pinned ESP-IDF build pass; the current image is 0x17bb80 bytes. The owner has
since said the board is unavailable. Subsequent changes through 2026-10-27
passed the pinned firmware build without device tests or flashing; see
[PROGRESS.md](PROGRESS.md) for details.

History series are selected from stored ROM IDs, so retired probes remain
available; the page displays avg/min/max and keeps visible gaps/partial groups.
UTC labels use browser-local time. Frontend tests, typecheck, lint and build pass.

History charts have touch-friendly bounded zoom (1×–8×) and half-window
Earlier/Later buttons. Its lazy-loaded bundle measured 10.36 kB raw / 4.19 kB
gzip; tests cover the bounds and reset.

The Alerts page distinguishes shared bus and probe states and pages the durable
event journal. It shows UTC when known, otherwise boot ID and uptime, and warns
when entries have been compacted or the journal is unavailable. Device-level
verification of persistence is pending.

Candidate Wi-Fi setup now waits one second after AP+STA activation before
station disconnect so the queued response can complete. The on-device
nonexistent-network test passed and kept setup available; no phone or station
client was used. See [Wi-Fi handoff results](validation/device-wifi-handoff-results.txt).

Embedded offline onboarding supports scan, manual SSID entry and retry/error
states; captive redirects and direct `/setup` passed checks. Phone/DHCP flow
remains unverified and needs an owner client.

The latest 2026-10-27 change extracted bounded history query parsing into the
host-tested `monitor_core` component. Parser tests exposed and fixed rejection
of repeated `rom_id` selectors, restoring multi-sensor history selection. Host
and pinned ESP-IDF v6.1 verification passed (firmware image 0x17c360 bytes,
63% OTA slot headroom). A follow-up host check persisted/reopened all 288 five-
minute records in an accelerated 24-hour run. `T06-14` remains open for actual
filesystem-full, simultaneous-export and hardware power-loss checks. `T07-10`
remains open for oversized request fuzzing and slow-client/device integration
coverage. The ESP32 is currently unavailable; do not treat host simulations as
peripheral or device verification.

The latest host run also includes deterministic single-byte mutations of a
valid history query, and the 24-hour aggregation test now persists and reopens
all 288 five-minute records. The full host verifier passes; actual filesystem
exhaustion, simultaneous exports, slow-client device behavior and physical
power-loss checks remain open.

Settings now includes a confirmed data-preserving restart request at
`POST /api/v1/system/reboot`, guarded by the shared session/Origin/CSRF checks
and delayed until after acknowledgement. Host tests and the pinned firmware build
pass (image 0x17c8a0 bytes). The device API script now covers authorization,
body rejection, restart-to-READY, and session invalidation, but was not run
because the ESP32 is unavailable. Physical restart acceptance and a full
factory reset remain open under T09-15.

`tools/device_api_test.py` now also has pending history API checks for malformed
ROM IDs, five/six selectors, point-count bounds, and oversized query strings.
Only its syntax has been checked; device API execution remains unavailable.

The host alert integration now covers disconnect/reconnect for all five probes
in reverse order, distinct configuration and calibration, and sixth-probe
rejection. A `/dev/full` test injects `ENOSPC` and verifies failed appends are
reported. The full host verifier passes and `T10-07` is checked. Actual LittleFS
exhaustion, concurrent export, and physical power-loss checks remain open under
`T06-14/16`.

The WebSocket simulator test now injects a stalled send socket and confirms the
configured buffer limit terminates it; its four-client cap and 30 reconnects
also pass. Firmware socket backpressure still needs device acceptance.

The USB device API script additionally rejects a JSON body over 2,048 bytes;
only Python syntax has been checked. Run it on the ESP32 to validate server
handling of oversized bodies and connection cleanup.
