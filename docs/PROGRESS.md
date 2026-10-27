# Implementation progress

## Current checkpoint — resume here

The owner requested a stop and a handoff after finishing the history/CSV work.
Resume from [HANDOFF.md](HANDOFF.md) and the remaining items in
[TODO.md](TODO.md). Captive DNS, DHCP DNS configuration, OS probe redirects and
live-client race fixes were verified before this handoff.


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
are implemented; physical probe/LCD checks and durable event history remain open.


66 named TODO items are checked with software/device evidence. The remaining
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

The LCD and probes should remain disconnected for now. Their electrical and
physical gates are intentionally deferred; GPIO15 remains untouched. The board
is currently unavailable; the flashed image is unknown after an interrupted
normal-image restore. Do not report a successful restore without rechecking.

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
normal firmware image is 0x1699d0 bytes against a 0x400000-byte OTA slot.
The ESP32 was unavailable for this final build, so it has not been flashed or
tested on-device. T07-05 stays open because absent entire history windows do not
yet have explicit gap markers in the HTTP contract.


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
remain outstanding.

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
