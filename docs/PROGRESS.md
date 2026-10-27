# Implementation progress

## Current checkpoint — resume here

Ralph loop resumed on 2026-10-27 from [HANDOFF.md](HANDOFF.md). The owner says
the ESP32 is currently unavailable; do not attempt device tests or connect the
LCD/probes until it returns. Captive DNS, DHCP DNS configuration, OS probe
redirects and live-client race fixes were verified earlier.

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


97 named TODO items are checked with software/device evidence. The remaining
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
