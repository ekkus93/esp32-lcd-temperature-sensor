## 2026-10-27 — cancel stale AP handoff deadline on Wi-Fi retry (software-only)

Previously a successful candidate scheduled setup AP shutdown after 30 seconds,
but starting a subsequent candidate left that timer active. If the new attempt
failed, the stale timer could close the AP just after failure, preventing retry.
The production Wi-Fi worker now cancels the prior close deadline at the start
of each candidate, and uses a shared small `WifiApWindow` timing policy.
A new native CTest exercises ten-minute manual setup recovery, the 30-second
successful handoff, failure/retry cancellation, and overlapping timer events.
No owner Wi-Fi settings or radio were used. `T05-04` remains open pending
station/DHCP and saved-network verification on a permitted device.

## 2026-10-27 — closed WebSocket peer safety (software-only)

The production RFC 6455 nonblocking sender now also supplies `MSG_NOSIGNAL`
when the platform defines it, retaining `MSG_DONTWAIT` on all platforms.
This ensures a peer that disconnects just before a queued send cannot cause a
process-wide POSIX SIGPIPE. Added a real-socket sender regression and a
production `WsLive` broadcaster test with one already-closed peer, two healthy
WebSocket subscribers and a concurrent HTTP connection. These tests are
hardware-independent and do not exercise ESP32/LwIP session-close completion
or establish the firmware TCP slow-client cutoff. Leave `T07-10` unchecked.

# Implementation progress

## 2026-10-27 — reject empty inbound WebSocket commands (software-only)

The production `WsLive::receive` handler no longer silently accepts zero-byte
text or binary commands on its read-only live endpoint. Empty Ping/Pong
protocol control frames retain their prior permitted behavior; nonempty
unsolicited messages trigger closure, while malformed/oversized headers and
receive failures return an error. The HTTP/WebSocket host shim covers these
frame-type distinctions, the 1,024/1,025-byte boundary, and read failures.
An additional local native mock compile/run passed without an ESP32-S3.

This improves `T07-10` software coverage but does not establish physical
ESP32 TCP backpressure, session-eviction timing, or Wi-Fi endurance. `T07-10`
remains unchecked pending device acceptance; no device was accessed.


## 2026-10-27 — byte-by-byte interrupted history write recovery

Added `history_power_cut_tests` to simulate interrupted TMH2 writes at every
byte prefix for one- and five-probe records. Each case exercises both an
interrupted append within an active segment and one immediately after a
segment rollover, restarts the store, verifies the completed record is
preserved, and successfully appends the next record. This covers 568
deterministic host recovery/restart cases; 11/11 native CTests pass.
The WebSocket regression also now uses a real POSIX socketpair with a tiny
send buffer to assert bounded nonblocking behavior. Corrected include order
after the first exact-head CI detected two clang-format failures, and moved
the format check earlier in CI.

None of this proves physical LittleFS power-loss behavior, ESP32-socket
eviction, or real Wi-Fi recovery. Keep `T06-14` and `T07-10` unchecked.


## 2026-10-27 — hardware-independent WebSocket send hardening

Extracted the production RFC 6455 nonblocking frame sender into `ws_frame` so
the ESP32 path and native regression tests compile the **same implementation**.
The sender now uses a shared 16-call budget across header and payload, preventing
pathological short writes or repeated `EINTR` from monopolizing the HTTP worker.
It returns failure on `EAGAIN`, a closed/broken socket, or exhausted budget;
`ws_live` requests session closure on any failed or partial frame. Native tests
exercise framing across all three payload-length encodings, partial writes,
`EINTR`, `EAGAIN`, `EPIPE`, `ECONNRESET` and bounded calls.

Sandbox native CMake/CTest: **10/10 passed** (new suite `websocket_send_backpressure`).
This is deterministic injected transport evidence only; it does not demonstrate
ESP32/LwIP TCP-window exhaustion or verify that the async HTTP session close
actually completes. Keep `T07-10` unchecked until physical device acceptance.
No LCD, 1-Wire probe, Wi-Fi credentials or destructive operation were used.


## 2026-10-27 — factory reset options and current validation

Settings now presents two separately confirmed factory-reset choices. Both
clear NVS credentials and device settings; only the second erases the LittleFS
history partition. A committed mode marker is applied at early startup before
history workers run. The API requires an authenticated CSRF-protected request
with exactly one `erase_history` boolean. Generated response contract, tests,
user/history docs, and TODO descriptions were updated.

Validation passed: 7 focused API/UI tests, all 56 web tests, Prettier, ESLint,
TypeScript, production build, full `tools/verify.sh` (9 host CTests, simulator,
display, web suite), and ESP-IDF v6.1 firmware build (0x1e4f40 bytes, 53% app
slot headroom). No successful destructive reset was issued. The ESP32 is
currently unavailable; keep `T09-15` open until a disposable acceptance target
verifies both data-retention modes. No LCD or probes were attached.

## 2026-10-27 — LittleFS initialization gate regression

Extracted the history partition's all-erased decision into the same inline
predicate used by the ESP-IDF mount worker. Host storage tests now check erased
media permits initialization only after a mount failure, while missing/empty
buffers and programmed bytes at multiple offsets never permit formatting.
Existing corruption recovery cases verify nonblank history remains preserved.
T06-03 is checked for software behavior; physical mount-fault and power-loss
acceptance remain open under T06-14/16. Full verifier and ESP-IDF v6.1 build
pass: 9/9 host tests, simulator, display test, 56 web tests, formatting, lint,
typecheck and production build; firmware image 0x1e4f90 bytes with 53% slot
headroom. TODO count is 114 checked and 44 open.

## Current checkpoint — resume here

The 2026-10-27 LCD software checkpoint adds the `esp_lcd` ST7789 SPI and LVGL
runtime using GPIO 12/11/10/9/14, two 240×40 PSRAM/DMA buffers, and a lock around
view updates. GPIO15 remains unused. Optional `CONFIG_MONITOR_LCD_TEST_PATTERN`
shows color bars and corner markers; no panel or sensor was connected. Host
render coverage now includes live temperature repaint before a history bucket,
offline/connecting/connected Wi-Fi state changes, and time becoming available.
`T03-08`, `T04-10`, and `T04-11` are checked. `T04-01`, `T04-03`, and the other
physical display gates remain open pending panel validation. The full
`tools/verify.sh --firmware` passed (9 CTests, simulator, headless LVGL, 53 web
tests, lint/format/typecheck/build, pinned IDF build); image 0x1e3fd0 bytes with
53% OTA slot headroom. No flashing was performed. TODO: 102 checked, 56 open.

Clock-jump follow-up: host review found that two partial aggregates could share
one UTC five-minute key after an NTP step within the same wall-clock window.
The aggregator now stores such intervals with boot-relative uptime and resumes
trusted UTC buckets only at the next unoccupied boundary. A regression covers
the step, uptime-only interval, and UTC recovery. DST conversion and monotonic
display cycling also pass host tests. `T05-17` is checked; real SNTP/device
behavior still needs the board and an owner test network. The pinned full
verifier passes again; image is 0x1e3fd0 bytes with 53% OTA slot headroom.
TODO count: 106 checked, 52 open.

Production preview follow-up: Vite preview now proxies API and WebSocket traffic
to the loopback simulator. Chromium measurement of the built five-probe
dashboard recorded 511 ms to load, first status at 763 ms, 188,551 bytes of
initial HTTP transfer, and 6.3 MB browser JS heap after 16 seconds with a live
socket. Asset totals remain 639,363 raw / 203,464 gzip bytes across 14 files.
`T09-20` is checked; device heap/socket profiling remains T10-05. Web tests,
lint, format and production build pass. TODO count: 107 checked, 51 open.

Clock-service follow-up: host tests now exercise cold-start unknown UTC, monotonic
time progression during an offline interval, an unsynchronized aggregate, and
same-boot recovery into trusted UTC after synchronization. The configured NTP
hostname/timezone path, SNTP callback quality flag and DST formatting were
reviewed and the settings/DST tests pass. `T05-14/15/16` are checked for
software behavior; real association, SNTP sync and radio-loss acceptance still
require the board and owner network.

Ralph loop resumed on 2026-10-27 from [HANDOFF.md](HANDOFF.md). The owner says
the ESP32 is currently unavailable; do not attempt device tests or connect the
LCD/probes until it returns. Captive DNS, DHCP DNS configuration, OS probe
redirects and live-client race fixes were verified earlier.

## 2026-10-27 — bare-board smoke resumed

The ESP32-S3 is now visible as `/dev/ttyACM0`. The read-only 20-second
`tools/device_smoke.py` check passed: core/auth self-tests, healthy history
mount, READY with zero sensors and sensor bus/LCD/backlight disabled, positive
free heap, and four health reports at roughly five-second intervals. No panic
or fabricated readings appeared. Detailed evidence is in
[device smoke results](validation/device-smoke-2026-10-27.txt). No LCD or
probes were connected, and no firmware was flashed. The old unavailability
note above is historical; board-dependent work still needs peripherals and,
for station/NTP tests, owner Wi-Fi credentials.

The isolated simulation image also completed the expanded two-ROM sensor
settings test. Distinct labels, limits and calibrations survived a protected
ESP32 restart without cross-contamination; invalid/stale updates and live
calibration/alert behavior passed. The normal firmware was restored and passed
the bare-board smoke again. `T03-10` is now checked. Evidence and commands are
in [sensor NVS results](validation/device-sensor-nvs-results.txt). No LCD or
physical probes were connected.

The regular image then passed the current USB-loopback device API/security
suite, including OTA rejection, bounded Wi-Fi scan, WebSocket auth/origin,
malformed and oversized requests, event-journal pagination, login
cooldown/cadence, and safe restart/session invalidation. The leak scanner found
no local secrets in 241 reviewable files/logs. The exercise exposed and fixed a
test-bridge URI limit that prevented oversized query testing. Evidence is in
[device API results](validation/device-api-results.txt). `T05-07` and `T08-08`
are checked; `T07-10` still needs slow-client and concurrent-device-load checks.
The USB loopback now captures actual ESP32 WebSocket frames as well. Complete
snapshots arrived 4,750 ms apart with nondecreasing sequence values; a masked
client command closed the read-only stream without affecting later API calls.
`T07-08/09` and the evidence/checklist package `T11-07` are checked;
`T07-10` retains slow-client/concurrent-load work. Current required TODO count:
113 checked, 45 open.

The durable event journal is implemented in LittleFS (128 KiB cap, CRC records,
compaction, tail recovery, cursor pagination). Eight host CTest suites, the
headless renderer suite, 49 web tests, lint, typecheck, production build and
pinned ESP-IDF v6.1 firmware build passed. No device verification of the new
event journal was run. T06-14/16 power-loss and persistence checks remain open.


Sensor configuration REST/UI and bounded in-memory alert views are implemented.
Eight core host suites, the LVGL render suite and 30 frontend tests pass. The real ESP32 simulation build
passed calibration, alarm/disable transitions, malformed/duplicate fields,
missing/stale If-Match rejection, and NVS settings persistence after restart.
Simulation data is isolated from real settings/history. See
[the sensor API results](validation/device-sensor-results.txt).
History query/reduction is integrated with LittleFS and the web History page;
the ESP32 simulation passed persisted query/filter/bounds checks. The normal
firmware passed a real radio scan (nearby networks, no association) and its
rate limit, plus the complete protected API suite. The main image is about
1.48 MB against each 4 MiB OTA slot. Historical label metadata and CSV export
are implemented; physical probe/LCD checks and event-journal device verification remain open.


98 named TODO items are checked with software/device evidence. The remaining
items are open; this is not a release-ready implementation. Current validation
is recorded in [the baseline report](validation/2026-10-25-baseline.md),
[device API results](validation/device-api-results.txt), and
[device cadence capture](validation/device-api-cadence.json).

The next device gate needs an owner-supplied local JSON file containing `ssid`
and `password`. Its absolute path has been requested; no network credentials
have been supplied yet. Do not print its contents. Resume with the pinned IDF
environment and `tools/device_setup.py --port /dev/ttyACM0 --wifi-file PATH`, then
verify DHCP/candidate failure recovery, credential persistence, clock sync and
runtime BOOT provisioning. A phone onboarding check remains separate.

The LCD and probes remain disconnected. Their electrical and physical gates are
deferred; GPIO15 remains untouched. On resume, the running image reported
`bus=disabled`, `sensors=0`, and healthy history with no panic.

Local HTTP was explicitly accepted by the owner. Development setup/admin
credentials are in ignored mode-0600 files under `.build/`; the real device
administrator was created by automated tests. Use those private files for
local development, never attach them to reports. The owner committed and pushed
checkpoint `1a496b6`; this handoff captures subsequent work. No release or
deployment has been performed.

## 2026-10-26 — History format and CSV handoff

Binary `TMH2` records retain per-ROM UTF-8 names and state flags, including
zero-valid-count windows. Legacy `TMH1` remains readable without inventing
historical names. Authenticated CSV export streams full five-minute records with
simulation, timestamp-quality, ROM and validity fields. The web History page
links to the export and labels its resolution. See [format details](HISTORY_FORMAT.md).

The real ESP32 simulation passed the CSV cases in
[device-history-csv-results.txt](validation/device-history-csv-results.txt):
authentication, persisted names/flags, unknown UTC, simulation markers and
query rejection. `tools/verify.sh --firmware` passed locally at handoff: eight
host CTest suites, one headless LVGL suite, clang-format, Prettier, ESLint, 30
Vitest tests, TypeScript/Vite build, exact SDK check and firmware build. The
normal firmware image at that checkpoint was 0x1699d0 bytes against a
0x400000-byte OTA slot. The ESP32 was unavailable for that build. At the time,
T07-05 remained open because absent history windows had no explicit API marker;
it was completed and device-checked on 2026-10-27 (see the current checkpoint).

## 2026-10-27 — Sensor cadence and bus fault handling

The resumed firmware verification passed eight host CTest suites, the LVGL
renderer, 30 frontend tests and the exact ESP-IDF v6.1 build. Normal app image
size: 0x169ed0 bytes. A 12-second USB capture from the live board showed three
zero-sensor health reports at expected cadence, available heap and healthy
history. Device sensing remains disabled without probes.

The RMT service sorts/caps discoveries at five ROMs, scans every 30 seconds,
and retries communication failures after five seconds. Failed shared-bus
operations and a known-device cycle with zero completed transactions report one
bus-wide fault; one failed sensor transaction follows the per-sensor invalid/
disconnect path. Host tests cover classification. No electrical stuck-line or
physical sensor test was performed.

The API response schemas now cover snapshots, preferences, sensor
configuration/patches, history, alerts, auth state, Wi-Fi status and scan output.
`web/scripts/generate-contracts.mjs` regenerates the TypeScript types, and the
browser validates those API responses with Ajv. `contracts/API.md` records paths,
access rules, Celsius values and wire-order ROM IDs. The final
`tools/verify.sh --firmware` passed eight core suites, the LVGL suite, 33 web
tests, lint/format/typecheck/build and pinned IDF build; image size is
0x16a220 bytes. The live board also passed normal-image smoke and authenticated
API/radio-scan checks. No sensors or LCD were connected.


## 2026-10-25 — Initial implementation

This is a development build; the full TODO and release gates remain open.
No release, Git push or deployment has been performed.

### Verified

- Pinned ESP-IDF v6.1 at `fff9895c82d744c7237be8847347bdd1b07c6643`.
- Connected board identified through esptool: ESP32-S3 revision 0.2,
  16 MB flash, 8 MB embedded PSRAM. USB Serial/JTAG at `/dev/ttyACM0`.
- Initial firmware compiled and flashed; application version 0.1.0 started,
  PSRAM test passed, and 5-second health messages reported zero sensors.
  Initial application: 199,360 bytes; internal free heap: 386,731 bytes;
  total free heap: 8,772,923 bytes. These are bare-board baseline readings.
- Host CTest suites cover core behavior and history corruption/rotation,
  including 24 simulated hours yielding exactly 288 five-minute windows.
- React 19/MUI 9 preview passes initial unit tests and strict typecheck/build.
  Initial production assets: about 577 KB raw / 179 KB gzip.
- Component resolution recorded in the firmware lockfile: 1-Wire 1.1.2,
  DS18B20 0.4.0, LVGL 9.6.0~1, LVGL port 2.9.0, LittleFS 1.23.1.

### Current implementation boundaries

`monitor_core` contains host-testable registry, CRC/reading validation,
alerts, time-quality mapping, BOOT/display timers, aggregation and binary
history storage. `onewire_service` owns one RMT bus; it remains disabled in
bare-board configuration. The firmware now includes protected AP setup, HTTP administration and embedded
web assets; radio association is still unverified. The web app uses
validated snapshots and honest empty/error states; several pages remain
previews rather than completed configuration/history features.

### Open gates

- Connect no LCD or probes yet. Sensor and LCD electrical/physical checks are
  deferred by user instruction. GPIO15 must remain untouched pending review.
- Pinout orientation, regulator current budget, other USB port, LCD dimensions,
  backlight circuit and physical probe reliability are not verified.
- Network onboarding/authentication, full history integration, LCD rendering,
  OTA and final release gates are not complete.
- GitHub repository exists; local documents/code have not been imported/pushed.
- The original macro-keyboard application was overwritten with permission.

### Recovery and evidence

Use `./tools/verify.sh` for repeatable software validation and
`tools/device_smoke.py` for bare-board health checks. The device test saves its
capture under `.build/`, which is ignored. Startup logs can include device IDs
but must never include Wi-Fi/admin/session secrets.

During setup, an npm command briefly targeted `/home/phil/work` before the web
manifest existed. The affected existing `yahoo-finance2` dependency was restored
from its cached 3.13.2 version, and the newly created parent package manifests
were removed. Project npm commands now target the explicit `web/` package.

### Further implementation and checks

- Added NVS settings adapter, preserving invalid/unknown settings instead of
  erasing them, and host round-trip/corruption/schema tests.
- Added a bounded history worker (four queued buckets), LittleFS initialization
  only for a fully erased partition, and 3 MiB temperature-history retention
  within the 4 MiB filesystem. Completed segments are preserved on corruption;
  only damaged tails with no later valid records may be truncated.
- One-Wire adapter compiles against pinned ESP-IDF v6.1 and uses one broadcast
  conversion followed by ROM-addressed CRC-validated reads. Physical sensor
  verification remains deferred; compile/host evidence is not sensor hardware
  acceptance.
- Added a loopback C++-backed REST/WebSocket simulator and live-client tests for
  gaps, malformed messages, silence and reconnect. Preview dashboard with five
  probes has no horizontal overflow at 360 and 1280 CSS pixels in Chromium.
  This is a dashboard check, not acceptance of all unfinished pages.
- Device core self-tests passed in the diagnostic image, followed by normal
  five-second health samples. Updated images continue to require device checks.
- Proposed security/enrollment flow is documented in `SECURITY_DESIGN.md`;
  transport/trust choice is pending user input before network credential flows.

### Earlier security checkpoint (resolved)

The owner subsequently selected local HTTP and explicitly accepted its LAN
confidentiality limitation. The network/auth implementation below followed
that decision; unimplemented UI pages and release gates remain unchecked.

Software validation: `./tools/verify.sh` passed host CTest (three suites),
clang-format, Prettier, ESLint, nine Vitest tests, strict TypeScript and Vite
production build. Core tests also run on ESP32 startup. Python helpers compile,
and shell scripts pass `bash -n`. The initial raw JS chunk exceeds Vite's 500 KB
advisory; the full app is about 179 KB gzip. Secondary-page splitting remains
open while those pages are implemented.

Owner decision: local HTTP selected on 2026-10-25; documented LAN confidentiality
limitation explicitly accepted. Continue network enrollment and session work.

## Network/authentication work after HTTP acceptance

Added initial HTTP serving of compressed React assets, bounded snapshot publishing,
pre-handshake WebSocket authorization, per-device WPA2 setup credentials,
PBKDF2-HMAC-SHA256 administrator enrollment, expiring RAM sessions, CSRF and
Host/Origin checks, and candidate Wi-Fi control with delayed credential commit.
These features are under validation; their TODO items remain open until checks
pass. The default sensor bus and all LCD/backlight outputs remain disabled.

`tools/device_setup.py` retrieves setup credentials through physical USB into a
mode-0600 file under ignored `.build/`. `tools/device_api_test.py` exercises the
real HTTP/auth stack through a USB loopback bridge; it saves a random development
administrator password privately instead of embedding a known default. This
bridge does not verify Wi-Fi radio behavior. A local owner-supplied Wi-Fi
credentials file has been requested for network tests.

The initial bridge attempt did not complete because USB startup/command framing
needed handling; console accumulation and client startup waits were added. Do
not record the device API suite as passed until its full rerun succeeds.

## Verified network/auth checkpoint

Fresh owner enrollment returned HTTP 201 after physical USB recovery. Correct
password verification, authenticated snapshots/WebSocket upgrades, rejection of
forged Host/Origin and missing CSRF, malformed/deep JSON and oversized SSIDs,
bootstrap reuse rejection, logout, and failed-login cooldown all passed on the
real device HTTP stack. Static gzip HTML and its hashed JavaScript path also
passed through the physical USB loopback test.

The initial enrollment response issue was the test bridge's timeout near the
measured ~15-second KDF duration; fresh-state rerun with adequate timeout passed.
HTTP/KDF work is now isolated on CPU1. The latest concurrent authentication run
captured eight health samples at exactly 5,000 ms intervals. Host session tests
add expiry, token forgery, capacity eviction, logout and reset checks.

Final `tools/verify.sh --firmware` passed four CTest suites, clang-format,
Prettier, ESLint, twelve Vitest tests, strict typecheck, Vite and pinned firmware
build. The embedded asset manifest matches the latest Vite files. Sensitive test
values were absent from reviewable files and sanitized diagnostic reports.

The device SoftAP is visible from the host (100% signal). This proves radio
advertising only: no claim is made about association, DHCP, phone onboarding,
router recovery, actual sensor readings, LCD output or OTA acceptance yet.

## Subsequent sensor/history/network work

Sensor settings are keyed by immutable wire-order ROM, validated on both ends,
queued to the single acquisition owner and protected by boot/revision If-Match.
The UI refreshes its revision after application. Only configuration changes
write NVS; simulation has isolated settings/history. MCU simulation verified
calibration, high alarm, disable/re-enable and reboot persistence.

History queries bound ranges to 31 days and groups to 512; weighted reduction
preserves extrema and marks missing/partial windows. Unsynchronized queries
require a boot selector; UTC queries exclude unknown timestamps. Overlaps or
excess mixed-boot series are reported as truncation. Five host suites and the
real simulated-storage HTTP checks passed. CSV/export and durable event history
are implemented; device persistence and power-loss checks remain outstanding.

Wi-Fi scans run asynchronously, return at most 20 valid visible SSIDs, and have
a 10-second request limit and deadline. The real board detected nearby networks;
no owner network credentials or phone onboarding have been exercised. A normal
image is restored after simulation tests. Browser password-change UI and longer
timeout are implemented; actual password rotation, old-session invalidation and new-password login passed on the ESP32. Sampling cadence remained within its test bounds.

## Headless LCD renderer

The shared `display_service` LVGL 9.6 renderer compiles under IDF and renders
240×320 host PNGs. Overview and Trends objects persist across monotonic mode
switches. Five ROM-ordered rows distinguish empty/off/lost/invalid/stale/bus
fault and alarms; long labels ellipsize, including worst-case negative values.
The bounded RAM cache stores at most 288 five-minute windows, preserves gaps,
and feeds LVGL without filesystem reads. Temporary setup is a system screen,
not a selectable third normal view. See [software renders](validation/lcd-simulation.md).

The host render gate passes along with five core/storage/security host suites,
22 frontend tests and the exact-IDF firmware build. Physical panel/port/DMA
integration, timezone application and persisted web display settings remain
open. The firmware does not initialize the view or any LCD/backlight GPIO.

## Persistent device preferences

Versioned/checksummed device preferences persist separately from sensor identity
mappings; unknown or corrupt versions are preserved read-only. Authenticated
GET/PATCH settings use boot/revision If-Match, strict keys/types/bounds and a
shared JSON schema. The web form controls C/F, Basic/Extended, dwell times,
lookback, supported IANA timezone identifiers and the NTP hostname. Dashboard
units load from the device preference. Timezone rules are explicit POSIX mappings
with winter/summer host checks for every supported identifier. Clock reconfigure
keeps trusted running time; cold boot remains unsynchronized until an SNTP callback.

Six core host suites, headless LVGL rendering, 24 frontend tests and IDF builds
pass. Isolated ESP32 simulation tested malformed/duplicate fields, unknown
timezone/invalid server, stale revisions, NVS commit, clock reconfiguration and
preference persistence across restart. See
[device preference checks](validation/device-preferences-results.txt).
No claim of network association, real NTP/DST LCD output or physical display
integration is made. Normal image was restored; the full protected API/radio scan suite passed again, including a real snapshot with zero probes and simulated=false.

## Wi-Fi failure progress and browser checks

The browser polls explicit testing/succeeded/failed/storage-error outcomes.
Overlapping candidates return 409. Saved credentials change in RAM only after
a successful NVS commit; failure retains the prior record. On the real board,
a randomly named nonexistent SSID timed out with setup still active and station
configuration still blank. This tests failure, not successful owner-network
association, saved-network rollback or NTP. The full API suite and sampling
cadence checks passed afterwards.

Browser Settings passed 360/1280 pixel overflow checks in both themes. Native
Tab/Enter navigation showed focus and worked. Computed contrast checks found
the orange warning badge at 3.11:1 in light mode; the adjusted palette produces
5.79:1 there and 10.62:1 in dark mode. Other checked status/form/navigation text
exceeded 4.5:1. Twenty-five frontend tests pass.

## 2026-10-26 — Resumed checkpoint

Owner commit/push `1a496b6` imported source, specifications and mockups. GitHub
contents and rendered README relative documentation links were verified.
The resumed strict TypeScript build exposed a mock-resolver type mismatch in a
new test; it is fixed. Seven core CTest suites, LVGL renders, 28 frontend tests,
format/lint/strict typecheck and exact-IDF build all pass. The board was flashed
and passed full API/auth/radio-scan tests and captive HTTP/UDP tests.

Captive DNS binds only the AP address, advertises that DNS server through the
DHCP configuration, returns IPv4 AP answers with zero TTL and explicit AAAA
no-data responses, rejects compressed/malformed queries and bounds input to
512 bytes. OS connectivity probe redirects apply only while setup is active;
API Host/Origin checks stay enforced. Actual DHCP client/phone onboarding still
needs owner participation. See [captive checks](validation/device-captive-results.txt).

Live client regression tests now reject delayed REST replies after a newer boot,
callbacks from retired sockets and snapshots from retired boot IDs.

## 2026-10-27 — mDNS and API contracts

The firmware pins Espressif mDNS 1.14.1 and advertises
`temperature-monitor.local` with its HTTP service. On the real bare board, the
normal image initialized mDNS, passed a 15-second health smoke and the complete
protected API/radio suite. Setup AP fallback remains `http://192.168.4.1`; use
the station DHCP address where multicast DNS is blocked. The local hostname
has not been resolved by an associated client. See
[mDNS device results](validation/device-mdns-results.txt).

Versioned API JSON Schemas and generated TypeScript declarations now cover the
sensor, history, alert, auth, settings and Wi-Fi payloads; client requests and
responses validate at runtime. The full gate passed with eight core host
suites, one headless display suite and 33 frontend tests. At that checkpoint,
alert pagination and explicit markers for fully absent history windows were
still open; both were subsequently completed below.

Authenticated alert reads accept an `after` sequence and a 1–50 page limit,
return the next cursor, whether more events remain, and the oldest currently
retained sequence. Device API checks passed for empty history and rejected an
over-limit request. Alerts remain a 128-entry RAM ring for this boot; durable
event history is still open under T06-10.

Secret handling review found no browser local/session storage or console
logging in the web source. The active test-managed setup/admin secrets were
searched across 200 reviewable files and the build, smoke and API test logs;
there were no matches. The selected local-HTTP LAN exposure remains as recorded
in `SECURITY_DESIGN.md`.

The history response schema now includes explicit per-ROM/boot empty intervals
between returned points. Gaps inside downsampled groups retain the `partial`
flag; device queries stay bounded and CSV remains streamed. Eight core suites,
the display suite, 34 frontend tests and the bare-board empty-history API check
passed. See [history gap results](validation/device-history-gap-results.txt).

Independent Overview/Trends dwell settings are persisted through device
preferences and exposed in Settings. The LVGL view applies changes by restarting
its monotonic cycle, bounded from 5 to 300 seconds; the default is 20/10 seconds.
The host test covers runtime reconfiguration and invalid bounds. Physical panel
timing remains untested while the LCD stays disconnected.

History aggregation now has regression coverage for UTC five-minute alignment,
unknown-clock boot scope, restart isolation and a large NTP jump. A jump closes
the current aggregate as partial; records from separate boots are not merged.
The full host/display/web/IDF gate passed. Real SNTP association remains
untested without owner Wi-Fi credentials.

The isolated ESP32 simulation used fake probes and separate NVS to exercise
sensor-setting updates and persistence through a reset. Host codec tests reject
checksum damage, truncation, unknown versions and duplicate ROM identities.
The normal image was restored and passed a 15-second zero-sensor smoke. See
[sensor NVS results](validation/device-sensor-nvs-results.txt).

Dashboard cards now show per-probe last-reading age alongside label, temperature,
ROM and textual state. The shared row reports Wi-Fi, clock and logging health;
zero-probe state remains explicit. Frontend tests, strict typecheck and lint pass.

The History page now derives ROM series from persisted records rather than only
the live sensor list, so removed probes remain selectable. Per-series charts
show avg/min/max, break across missing intervals, label partial groups and use
browser-local time for trusted UTC. A retired-probe UI test and all 36 frontend
tests, typecheck, lint and build passed.

History charts now provide bounded 1×–8× zoom and half-window Earlier/Later
controls that wrap on small screens. The route remains lazy-loaded; the current
HistoryView chunk is 10.36 kB raw / 4.19 kB gzip. Tests cover zoom bounds,
panning and reset. Production build passes.

The Alerts page consumes the cursor-paged durable event journal, distinguishes
active shared-bus and per-probe faults, and shows UTC or boot ID/uptime. It warns
when compaction evicts older events or the journal is unavailable. Host tests,
web tests and firmware build pass; device persistence remains open under
T06-14/16.

Wi-Fi control now opens AP+STA setup, returns the queued `202` response and
waits one second before disconnecting the existing station. The device API
failure-path test passed scan, asynchronous acceptance, overlap rejection,
timeout and setup availability with blank prior credentials. This USB bridge
test did not associate a phone/client; success with owner credentials remains
open. See [Wi-Fi handoff results](validation/device-wifi-handoff-results.txt).

First-run onboarding UI completion is verified in software: embedded setup
assets, scan and manual SSID entry, retry/error states, captive DNS redirects
and direct `/setup` are covered. A phone/DHCP onboarding run still needs an
owner client and remains T05-18.

The Dashboard regression now shows bus-wide fault and per-probe high, disabled,
and disconnected states alongside a healthy probe without hiding sensor cards;
38 web tests passed (`T09-04`). Settings exposes the persisted LCD Basic/Extended
mode, independent 20/10-second dwell defaults, trend lookback and read-only
explanation; its focused UI suite passed (`T09-12`). The optional disabled-row
toggle is not implemented. Checked TODO count: 85 of 145.

Settings diagnostics now receive LittleFS capacity and used-byte counts through
the validated snapshot. The Settings panel shows storage health/capacity and
the five-minute aggregate policy; shared schemas and generated TypeScript were
updated. `npm --prefix web test` passes 40 tests with lint/typecheck/format and
production build; the pinned ESP-IDF build passes at 0x176b50 bytes (63% OTA
slot headroom). This advances `T09-11`; mount-failure fault injection is still
open under `T06-03`. Checked TODO count: 86 of 145.

Dashboard acceptance coverage now includes all five sensors with maximum-length
names; History tests explicitly cover an empty range and boot-relative time when
UTC is unknown. The Settings page covers both available capacity and storage
unavailable failure. Combined page tests exercise the T09-17 scenarios; the
full frontend suite passes 44 tests with typecheck, lint and format passing.
These are simulated fixtures, not physical-device validation. Checked TODO
count: 87 of 145.

First-run onboarding is complete in the embedded local SPA: owner pairing uses
the one-time setup secret, and network setup supports scan/manual entry, WPA
credentials, candidate progress and failure retry. Existing UI tests and device
portal redirect/asset checks support `T09-13`; physical phone/DHCP flow remains
T05-18. Checked TODO count at that checkpoint: 88 of 145.

Added a protected OTA upload route that writes only the inactive app partition,
enforces its size limit and octet-stream type, validates the image with
`esp_ota_end`, selects the boot slot and schedules restart after the 202
response. Authenticated/CSRF-less/empty-upload rejection checks are in the
device API test script. ESP-IDF v6.1 build passes (0x179f20 bytes, 63% slot
headroom); no device upload was run because the board is unavailable. T10-01
remains unchecked pending on-device acceptance.

Bootloader rollback is enabled in the pinned SDK configuration. Startup now
checks two acquisition/publish cycles before cancelling rollback for a pending
OTA image. The firmware rebuild passes at 0x179cb0 bytes (63% OTA slot
headroom). Forced-failure rollback and LCD update-state display are still
required for T10-02; no image was flashed.

The snapshot contract now includes running firmware version, partition and OTA
state, generated into TypeScript and shown in Settings diagnostics. The host
simulator supplies the same fields. The complete web suite passes 44 tests and
the ESP-IDF v6.1 build passes (0x179f20 bytes, 63% slot headroom). Safe reboot
and confirmed factory-reset controls remain open under T09-15.

## 2026-10-27 — Durable event journal and reliability diagnostics

Added a LittleFS event journal capped at 128 KiB with CRC records, global
sequences, compaction, duplicate suppression, partial-tail recovery and refusal
to accept completed-record corruption. It records sensor/bus, Wi-Fi, clock,
reboot and storage events. The API and Alerts UI now page persisted records,
show journal health, and use UTC or boot ID/uptime. Host recovery and pagination
tests pass; the device API test was updated but not run because the board is
unavailable. `T06-10` is checked; device persistence and power-loss acceptance
remain under `T06-14/16`.

Added `RELIABILITY.md` documenting accepted local HTTP exposure, the configured
watchdog timeouts, safe NVS/LittleFS failure behavior, startup health reports,
and the current bundle measurements. Startup now logs numeric reset reason.
`T10-06` is checked; heap minima, socket costs and device stability remain
unmeasured. Full `tools/verify.sh --firmware` passed: 8/8 host suites, 1/1
headless display suite, 44 frontend tests, lint/format/typecheck/build and the
pinned ESP-IDF v6.1 firmware build. Image size is 0x17b600 bytes (63% OTA slot
headroom). No device was flashed. Checked TODO count: 91 of 145.


## 2026-10-27 — Firmware upload control

Settings now includes a confirmed firmware image upload flow. The client limits
images to 4 MiB, sends raw octets with the session and CSRF token, reports
validation errors, and tells the user when restart is accepted. Component/API
tests cover confirmation, rejection, size/type guards, authenticated request
headers and device errors. The full pinned firmware verifier passes; the Vite
build embedded 13 current assets (637,631 raw / 202,577 gzip bytes) in the same
application image as the API. `T10-01` and `T10-03` remain open until the
updated/rollback image and served app/API pair pass on-device. No device was
flashed.

## 2026-10-27 — Security and OTA test coverage

The USB HTTP bridge now preserves an explicitly requested Content-Type, allowing
the device API script to distinguish wrong-media rejection from invalid-image
validation. The script now checks both cases without expecting an accepted
image or reboot. These new device cases are not run while the board is
unavailable. `tools/check-secret-leaks.py` passed across 213 reviewable
files/logs. The pinned full verifier passes 8 host suites, 1 headless display
suite, 49 web tests, formatting, lint, typecheck/build, and ESP-IDF firmware
build (0x17bb80 bytes, 63% slot headroom). Checked TODO count remains 93/145.

## 2026-10-27 — Live channel simulator integration

Added an automated host integration check that starts the C++ simulator-backed
API, validates a complete initial snapshot against the JSON schema, waits for a
heartbeat with a higher sequence, and verifies that invalid JSON on the
read-only live channel closes with a policy violation. The simulator bounds
incoming messages and rejects client commands; `tools/verify.sh` now runs this
check automatically. Firmware implementation already sends full snapshots
from a four-slot client table; device stream/backpressure verification remains
open under T07-08/09. Standalone `npm --prefix web run test:simulator` passed.


## 2026-10-27 — History and OTA recovery documentation

`HISTORY_FORMAT.md` now documents LittleFS corruption handling, safe partition
backup/restore commands and offsets, the distinction between administrator
credential recovery and factory reset, and OTA rollback/USB recovery. It
explicitly notes that a full factory-reset action is not yet implemented. The
pinned esptool command syntax was checked locally; no device data was read or
written. `T11-06` is checked. Checked TODO count: 92 of 145.

Host storage verification now asserts the encoded size of maximum-label TMH2
records for one through five probes. `validation/history-retention-estimate.md`
records the measured byte counts and payload-only forecast; actual oldest/latest
device timestamps remain open under T06-15. The 8-suite host storage run passed.


## 2026-10-27 — User guide

Added `USER_GUIDE.md` for live readings, probe identity/configuration, alerts,
five-minute graphs/CSV, network changes, display preferences, firmware uploads,
and the selected local-HTTP security limit. It describes the LCD, reboot and
factory-reset gaps explicitly. Relative links resolve. `T11-05` is checked.
Checked TODO count: 94 of 145.


`ONBOARDING.md` now documents the device-unique AP/bootstrap credential source,
first-owner enrollment, station candidate rollback, five-second BOOT recovery,
and the accepted local-HTTP LAN limitation. It distinguishes implemented
software behavior from phone/button checks still open under T05. Relative links
resolve. `T11-04` is checked; count is 96/145.


The existing wiring guide was checked against T11-02 and the development guide
now contains the pinned SDK output, firmware build/configuration/partition
commands, and honest USB connector status. The second USB-C role is explicitly
unknown pending T02 hardware checks. T11-02 and T11-03 are checked; count is
96/145.

## 2026-10-27 — WebSocket transport component boundary

Extracted WebSocket client/session tracking, read-only frame handling and
coalesced broadcasts from `http_api` into the new `ws_live` ESP-IDF component.
The API retains authorization and origin checks and supplies snapshots through
a callback. The simulator validates complete initial/heartbeat snapshots,
sequence progression and invalid-frame rejection. At this checkpoint,
`tools/verify.sh --firmware` passed 8 host suites, simulator live integration,
1 headless display test, 49 web tests and pinned ESP-IDF v6.1 build (image
0x17bd00 bytes, 63% OTA slot headroom). No device test was possible; hardware
stream and slow-client behavior remained open. `T01-02` was still open for
sensor registry, sampling and alert boundaries (96/145 checked then).

## 2026-10-27 — Simulator reconnect coverage

The simulator integration completed 30 sequential WebSocket
connect/snapshot/close cycles to exercise reconnect churn. The updated
`npm --prefix web run test:simulator` passed. `T10-07` remains open: existing
host suites cover 24-hour aggregation, history rollover/corruption, clock
alignment/steps and settings blob corruption; a new Registry scenario exercises
200 threshold cycles and confirms the 128-event cap and monotonic sequence.
The alert suite also removes one configured probe and adds a replacement ROM,
checking that the old identity remains disconnected and its label stays put
while the new probe gets its own default settings. Full five-probe replacement
and true storage exhaustion still need dedicated integration scenarios. The
simulator also accepts four concurrent live clients and rejects a fifth; this
assertion passed with the reconnect integration.

## 2026-10-27 — Sensor registry and sampling boundaries

Extracted ROM-keyed registry types and implementation into `sensor_registry`.
Added `sampling_service` to select either physical 1-Wire readings or fake
readings, removing that selection from `main`. The full pinned verifier passed
with 8 host suites, simulator integration, headless renderer, 49 web tests,
lint/format/typecheck/Vite build and ESP-IDF v6.1 build. Image size was
0x17bdb0 bytes with 63% OTA slot headroom. No hardware test ran. `T01-02`
remained open for `alert_service`; checked count was still 96/145.

## 2026-10-27 — Alert service boundary

Created `monitor_types` and `alert_service` ESP-IDF components. Registry
identity/configuration is separate from alert debounce, persistence,
hysteresis and state transitions. Added direct alert tests for threshold raise
and clear, hysteresis, disconnect/recovery, bus fault and disable behavior.
`tools/verify.sh --firmware` passed 9/9 host suites, simulator integration,
headless display test, 49 web tests, formatting/lint/typecheck/Vite build and
ESP-IDF v6.1 firmware link. Image size is 0x17bdf0 bytes (63% OTA slot
headroom). All components named by `T01-02` now build; the task is checked and
the named count is 97/145. Hardware verification was not run.

Expanded `settings_tests` to store separate labels, limits and calibration for
two ROM IDs, reload them in reverse discovery order and verify values remain
attached to the correct sensor; the full host verifier passed these checks.
`device_sensor_test.py` now has matching two-ROM reset-persistence assertions.
Its device portion was not run because the ESP32 is unavailable. Corrupt-byte,
truncation and unknown-version rejection remain covered in the host codec tests;
no older deployed sensor-settings schema exists to migrate from. `T03-10`
remains open until the expanded isolated-device reboot check runs.

Extracted bounded history query parsing into the host-tested `monitor_core`
component and added cases for malformed ROM IDs, duplicate scalar parameters,
unknown keys, oversized queries, invalid bounds, and one-to-five ROM selectors.
This exposed and fixed an API bug: repeated `rom_id` parameters were rejected,
so users could not request several sensor series together. The full host verifier
and ESP-IDF v6.1 firmware verifier passed; the image is 0x17c360 bytes with 63%
OTA slot headroom. `T07-10` remains open for oversized request fuzzing and
slow-client/device integration coverage.

Extended the accelerated 24-hour storage test to persist each completed
five-minute aggregate. It verifies 288 records, bounded encoded bytes, segment
reads, and successful recovery with all records retained. `tools/verify.sh
--host` passed again (9/9 CTest suites, simulator live integration, headless
display, 49 web tests, lint/format/typecheck and Vite build). This remains a host
simulation; `T06-14` is open for actual filesystem-full, simultaneous-export,
and hardware power-loss/reboot checks.

Added deterministic single-byte mutation coverage over a valid history query;
every accepted mutation must still satisfy the query and sensor-count bounds.
The full host verifier passed again, including all 9 CTest suites, simulator
integration, headless display suite, and 49 web tests/build checks.

Added a data-preserving Settings restart action and authenticated
`POST /api/v1/system/reboot`. The dispatcher applies session, Origin/Host and
CSRF checks to the route, rejects request bodies, and schedules restart 1.5
seconds after its `202` acknowledgement. Added a response JSON Schema/generated
type and browser tests for CSRF use, confirmation, success, decline, and errors.
`tools/verify.sh --firmware` passed with 9/9 CTests, simulator integration,
display test, 53 web tests, lint/format/typecheck/build, and ESP-IDF v6.1 link;
image size is 0x17c8a0 bytes with 63% OTA slot headroom. Device restart timing
and the full factory-reset operation remain open under T09-15.

Extended `tools/device_api_test.py` to verify anonymous and missing-CSRF restart
rejection, nonempty-body rejection, a successful authenticated restart, return
to READY, and session invalidation. Python syntax compilation and `git diff
--check` pass. The device script was not run because the ESP32 is unavailable;
factory-reset semantics/action also remain open.

Extended the pending device API script with history endpoint checks for malformed
and CRC-invalid ROM IDs, selector counts at and above five, an excessive point
count, and an overlong query. `python3 -m py_compile tools/device_api_test.py`
and `git diff --check` pass. These request/response cases require the ESP32 and
were not run.

Added a five-probe registry integration test: configure distinct names,
thresholds, and calibration; disconnect all five through debounce; reconnect in
reverse discovery order; and verify stable ROM mappings/readings plus rejection
of a sixth probe. `tools/verify.sh --host` passed (9/9 CTests, simulator,
display, 53 web tests, lint/format/typecheck/build).

Added a deterministic append-failure test by linking the active segment path to
Linux `/dev/full`; `HistoryStore::append` observes the resulting `ENOSPC` and
reports failure without claiming bytes were stored. The full host verifier passed
again. With the prior alert, replacement, clock, reconnect, settings-corruption,
and 24-hour cases, all simulation criteria for `T10-07` now pass; it is checked.
This does not verify a physically full LittleFS partition. `T06-14` remains open
for concurrent export and hardware power-loss/full-partition checks. The named
TODO count is now 98/145.

Added deterministic simulator backpressure coverage by corking a test client's
server-side socket after its initial snapshot and asserting the send-buffer
limit terminates it. The standalone simulator test passes with the client cap,
invalid-frame rejection, and 30 reconnect cycles. This covers simulator
behavior only; firmware slow-client acceptance remains pending device access.

Added a pending device API case with a valid JSON body just over the firmware's
2,048-byte request limit; the bridge request stays below its own 4,096-byte cap.
`python3 -m py_compile tools/device_api_test.py` passes. The live endpoint check
was not run because the ESP32 is unavailable.

## 2026-10-27 — Browser measurements and simulator auth contract

Chromium 156 on Linux measured the cold production SPA auth screen through
local Vite preview: 619 ms to `load`, 186,348 compressed bytes transferred for
HTML/CSS/JS, about 590 kB decoded and 6.6 MB browser JavaScript heap. The preview
does not provide the device auth API (502), so dashboard/socket and ESP32
heap/socket impact remain open under T09-20/T10-05. The browser did confirm the
startup bundle renders. Fixed the host simulator's CSRF token to meet the
declared auth contract and added HTTP auth/status assertions to its live
integration. `npm --prefix web run test:simulator`, format check, lint,
typecheck and production build pass.

Re-ran `python3 tools/check-secret-leaks.py` with the private local device test
secrets available. It found no secret match across 237 reviewable files/logs.
The device security acceptance script remains unrun because the ESP32 is
unavailable.

The storage host test now runs two CSV-style `HistoryStore::visit` calls at
once against 48 persisted records and checks both complete exports match. The
full `tools/verify.sh --host` passes (9 core suites, display, simulator, 53 web
tests, lint, format, typecheck and build). This covers concurrent host reads;
the HTTP/service and physical LittleFS gates in T06-14 remain open.

Closed the software recovery-path portion of T10-04: the OTA request aborts
failed/incomplete writes without selecting them, bootloader pending-verify
supports rollback before startup health confirmation, and nonblank history
corruption remains preserved while sampling continues. `HISTORY_FORMAT.md` now
connects these behaviors to Wi-Fi/AP/USB retry and backup/restore steps. Real
interruption and forced-rollback acceptance remains pending hardware.

The pinned `tools/verify.sh --firmware` run passes after the concurrent-store
test and recovery documentation: 9/9 host suites, simulator, display, 53 web
tests and ESP-IDF v6.1 build. The linked image is 0x17c8a0 bytes with 63% OTA
slot headroom. CMake packed 14 embedded assets (639,363 raw / 203,464 gzip
bytes); T09-20 and T10-03 bundle figures were updated. No device was flashed.
Current TODO count: 99 checked, 59 open checkboxes (158 total).


## 2026-10-27 — physical WebSocket concurrency and current verifier

The current ESP-IDF v6.1 build was flashed through the authorized device-test
workflow. The bare-board smoke and non-destructive API suite passed with no LCD
or probes connected. Physical USB-loopback coverage accepted four authenticated
WebSocket clients, rejected a fifth with HTTP 503, received complete snapshots
on all four, and served an authenticated status request (HTTP 200) while all
clients were connected. The measured heartbeat interval was 5,000 ms. The API
suite also passed history bounds, malformed/oversized request handling,
factory-reset rejection cases, and safe restart back to READY; no valid reset
was issued. `T07-10` remains open for firmware slow-client behavior.

`source /home/phil/esp/esp-idf-v6.1/export.sh && tools/verify.sh --firmware`
passes: 9/9 host CTests, simulator reconnect/four-client/slow-client checks,
headless display test, 56 web tests, lint, formatting, typecheck, production
build, and firmware link. The image is 0x1e5ab0 bytes with 53% OTA slot
headroom. One earlier concurrent full-suite run had a transient dashboard test
failure; isolated rerun and the full rerun passed. TODO remains 114 checked and
44 open. No LCD or probes were connected.

## 2026-10-27 — guarded Wi-Fi candidate-failure rerun

On `/dev/ttyACM0`, the guarded `tools/device_api_test.py --candidate-failure`
run reconfirmed candidate submission returns 202, overlapping attempts return
409, a random nonexistent SSID times out, and setup AP remains available. The
test refuses to run if saved station credentials exist, so it proves blank-
configuration recovery only. It also passed the full API suite, four-client
WebSocket cap and authenticated safe restart. No peripherals were connected.
Successful owner-network association and preservation of an existing valid
network still require private owner credentials; T05-04/T05-11 remain open.

## 2026-10-27 — slow-client investigation and stop handoff

Added an experimental nonblocking WebSocket frame sender that asks the HTTP
server to close a client when a frame cannot be written, plus an opt-in serial
bridge that holds a tiny-window client while issuing status requests over one
persistent HTTP connection. ESP-IDF v6.1 build and full `tools/verify.sh
--firmware` pass (9/9 host CTests, simulator, display, 56 web tests, lint,
format, typecheck and production build; image 0x1e63e0 bytes, 53% slot
headroom). A 30-second device probe received 30/30 HTTP 200 responses (maximum
256 ms), but accumulated about 1.6 KB and did not observe the server cutoff.
The longer 120-second run did not yield an inspectable result. After a board
reset, the 10-second bare-board smoke passed: READY, healthy history mount,
zero sensors, backlight disabled. No LCD or probes were connected. Keep
`T07-10` open and review the uncommitted experiment before extending it.

Current TODO count: 114 checked, 44 open. See [HANDOFF.md](HANDOFF.md) for
remaining hardware, owner-network, disposable reset, OTA, stability and release
gates.


## 2026-10-27 — host production WebSocket broadcaster regression

Added a host integration test compiling the production `WsLive` broadcaster
and RFC 6455 sender with minimal HTTP/auth/time test doubles and actual POSIX
socket pairs. The targeted native executable passed with strict
`-Wall -Wextra -Werror -Wpedantic` flags. Cases cover four authorized
slots/fifth rejection, an ordinary HTTP client alongside WebSockets, a saturated
non-reading socket that is closed without blocking healthy peers, an expired
session and an unregistered socket being closed, healthy peers receiving exact
unmasked text frames, closed-slot reuse, queued broadcast coalescing, failed
queue retry, and invalid inbound-frame closure. The host suite is now wired
into CTest. This is *not* a device-level TCP/window or ESP-IDF HTTP-session
eviction proof; T07-10 stays open. No hardware, owner credentials, reset,
flashing or OTA operation was performed. Exact-head GitHub Actions verification
is required after the commit.

The first exact-head CI run for `7a989859` stopped at the C++ formatting gate;
no test or firmware result was claimed from that run. The follow-up commit
normalizes the new test/shim formatting and requires a fresh exact-head CI pass.


The exact-head CI for `727f2974` passed the C++ formatting gate,
all 12 native CTests (including the new production WebSocket integration
test), and the pinned ESP-IDF v6.1 compilation. It then failed in the
pre-existing Node simulator integration: a WebSocket snapshot timed out.
The simulator transmits its first snapshot synchronously on connection, but
the test previously attached its first message listener only *after* the
`open` promise resolved, allowing an initial-frame race. The test now
buffers messages from socket construction and consumes queued frames before
waiting, eliminating that timing gap. Exact-head CI must confirm the complete
web/simulator/display gate; no device-level slow-client proof is claimed.

The exact-commit GitHub Actions run `40613793567` for `a8039d38` completed
successfully (2026-10-28 01:19 UTC): the pinned ESP-IDF v6.1 firmware,
12 native host CTests including production WebSocket isolation, simulator
WebSocket integration, headless LVGL, and React validation/build gates passed.
This verifies software-only behavior, not real ESP32 slow-client eviction,
owner-network association, physical peripherals, power-loss or OTA rollback.
CI: https://github.com/ekkus93/esp32-lcd-temperature-sensor/actions/runs/40613793567

## 2026-10-27 — OTA status on the read-only LCD (software-only)

Wired the existing OTA partition state into the shared LVGL status model.
Both normal screens now show `OTA VERIFYING` for `new` or
`pending_verify` images and `OTA ROLLBACK` for `invalid` or
`aborted` images. The Wi-Fi/logging line remains visible; a valid
image returns to the usual history-lookback footer. The headless LVGL
regression checks both screens and all five state transitions, without
adding another user-selectable LCD view. The state is sampled by the
normal five-second update loop, not a new blocking polling task.

Exact-head GitHub Actions is the software acceptance gate for the
headless test, native host suites, React checks and pinned ESP-IDF v6.1
compilation. `T10-02` remains unchecked: no real panel visibility,
OTA success, forced rollback or bootloader behavior was exercised here.
No board, probe, Wi-Fi credentials, factory reset or OTA flash was used.

## 2026-10-27 — versioned station credential record hardening (host-only)

Added a shared, pure C++ station credential codec used by the firmware NVS load,
NVS commit and candidate validation paths. Existing 99-byte v1 station records
remain readable; new successful candidate connections write a 103-byte v2
record with CRC32, bounded zero-terminated fields and canonical zero padding.
Malformed versions, truncated records, invalid lengths, noncanonical padding
and checksum errors are rejected without erasing the saved blob or attempting
an invalid association. CRC32 detects accidental corruption; it is **not**
confidentiality or tamper-resistant authentication. No credentials are logged.

Native host test: `c++ -std=c++17 -Wall -Wextra -Werror -Wpedantic
-I firmware/components/wifi_service/include
firmware/tests/wifi_credentials_tests.cpp -o /tmp/wifi_credentials_tests &&
/tmp/wifi_credentials_tests` (equivalent isolated local compile): PASS.
Coverage includes 103 single-byte mutations, all truncated v2 lengths,
legacy v1 compatibility, empty/64-hex passwords, and invalid input bounds.
CTest integration and pinned ESP-IDF v6.1 build await exact-commit GitHub
Actions confirmation. T05-01/04/05/11/18 remain open for real Wi-Fi
association, DHCP, candidate rollback, saved-network continuity and onboarding.
No board, Wi-Fi credentials, factory reset, flash or OTA was used.

Exact-head CI for `4472c3aa` stopped at the C++ formatting gate before
running tests/firmware. The next commit fixes the three clang-format violations
reported by the runner (include order and two line wraps). It requires a new
exact-commit CI run; no firmware acceptance is inferred from the failed run.

CI `87a34f9f` again stopped at formatting: the firmware translation unit's
primary header must remain first and the new credential header belongs among
the remaining sorted includes. The header's two line wraps now pass; a fresh
exact-head CI is required for the corrected include order.

## 2026-10-27 — OTA boot-health policy (software-only)

Corrected the pending-verify OTA acceptance gate: two completed acquisition
cycles alone were previously sufficient even if required services had failed
startup. The new shared C++ policy also requires the history worker, Wi-Fi
stack, authentication, HTTP server and sensor sampling initialization to have
succeeded. A missing LCD/probe, offline router or unsynchronized NTP does not
block acceptance; the device is designed to operate in those states. A failed
required service leaves a pending image unconfirmed for rollback on a later
reboot, rather than falsely marking it valid. Host policy regression covers
zero/one/two cycles and each failed-service permutation. Isolated local C++17
compile with `-Wall -Wextra -Werror -Wpedantic` passed; exact-head GitHub
Actions pinned ESP-IDF v6.1 compilation and full test gates are pending.

This is not evidence of successful OTA, forced bootloader rollback or actual
firmware recovery. T10-02 stays unchecked. No board or owner device was used.

The first exact-head OTA-policy CI (`cd804630`) stopped at clang-format,
without executing tests or the SDK build. The follow-up replaces a wrapped
aggregate initializer with explicit named health fields and normalizes the
host test's namespace declaration. A fresh exact-head CI pass is required.

## 2026-10-27 — exact-commit verification of both software changes

The Wi-Fi station credential codec change passed GitHub Actions at exact commit
`46c71c45425a6769854102889a4222524a555c58`, run
[40619609849](https://github.com/ekkus93/esp32-lcd-temperature-sensor/actions/runs/40619609849).
The OTA startup-health policy change passed at exact commit
`7a8118552b9882421aab2a61b73a39c45fa959d4`, run
[40620255967](https://github.com/ekkus93/esp32-lcd-temperature-sensor/actions/runs/40620255967).
Both completed successfully, including C++ formatting, native host suites,
simulator and headless LVGL tests, React verification and pinned ESP-IDF v6.1
firmware compilation. The second run also uploaded a firmware binary and build
manifest. These are software/build gates, **not** proof of real Wi-Fi station
association, OTA success/rollback, LCD/probe operation or flash power-loss
recovery. The checked/open TODO count remains 114/44. No owner credentials,
physical peripherals or reset device were used.
