## 2026-10-28 — event journal interrupted-write and forward-format preservation (software-only)

Added a native C++ regression that writes every 0–59-byte prefix of a
second TME1 event after a committed record, reopens the journal, verifies
the committed event and sequence, and appends another event successfully.
Recovery now refuses to truncate partial future-format headers after only
four or five bytes, as well as six-byte incompatible headers. This protects
unknown data across firmware version changes. The test is registered with native CTest. Initial exact-commit CI
`6ee5fd61` (run 40677966521) failed C++ formatting; `584d71bf`
(run 40678201043) also failed formatting. After fixing include order,
`2689085d` (run 40678337707) compiled the pinned ESP-IDF v6.1
firmware and ran the host suite, where the new test exposed an incorrect
assumption about an incomplete yet compatible TME1 header. The regression
was corrected to reject a future format only once its incompatible byte
is present. Exact-commit CI `98df8e1eb704f707aaf8b78c2561bb1a6d5abc90`
(run 40678870619) **passed**: 18 native CTests including event-journal
interruption, simulated REST/WebSocket, headless LVGL, React checks,
formatting and pinned ESP-IDF v6.1 firmware compilation.
T06-14/16 remain open for real ESP32 LittleFS exhaustion, simultaneous
device exports, physical power interruption and reboot evidence. No
hardware, owner network, credentials, flashing, OTA or reset was used.

## 2026-10-28 — fail closed on damaged diagnostic log records (software-only)

The numeric-only physical probe qualification tool now counts malformed
`SENSOR_DIAG`-marked lines in both saved-log and read-only serial modes.
Previously, malformed records were silently discarded, allowing a pair of
valid surrounding snapshots to pass despite damaged intervening evidence.
Qualification now fails if any marked record is malformed; unrelated log
lines are ignored, and only numeric counts (never raw logs, probe IDs,
credentials or temperatures) enter the report. Python host regressions
exercise a malformed interval, truncated marker, and CLI JSON output
without echoing source text. Exact-commit GitHub Actions `9ec3ff3c21ec412401da70e33d2fb154e768299d`
(run 40670068891) **passed** native C++/Python host tests,
simulated REST/WebSocket, headless LVGL, React checks and pinned
ESP-IDF v6.1 firmware compilation. Physical five-probe qualification
remains open under T02-15.
No device, Wi-Fi credential, flash, reset, OTA or power-loss test was used.

## 2026-10-28 — fail-closed physical diagnostic parsing and interval faults (software-only)

The read-only `SENSOR_DIAG` analyzer now rejects ambiguous or damaged
numeric records instead of silently overwriting duplicate fields or parsing
partial values. The parser requires the exact expected ordered fields, so
concatenated UART output cannot manufacture a passing health snapshot.
The analyzer also rejects a fault-cycle counter increase larger than the
number of new acquisition cycles, even when both cumulative counters
individually appear plausible. Added Python host regressions for duplicates,
concatenation, malformed fields, non-ASCII digits and impossible fault
intervals. This does not establish physical 1-Wire reliability, and
`T02-15` stays unchecked. Exact-commit GitHub Actions CI `f36e8839a0136f7ca1891043aca945f28e76c704`
(run 40661726628) **passed** native C++ and Python host tests,
simulated REST/WebSocket, headless LVGL, React verification and pinned
ESP-IDF v6.1 compilation. No board, network credentials, LCD, OTA,
reset or power-loss test was used.

## 2026-10-28 — reject replayed diagnostic evidence (software-only)

The numeric-only physical-probe report previously accepted repeated identical
`SENSOR_DIAG` snapshots as distinct health records when acquisition cycles
advanced only between the first and last lines. That could inflate
`--min-records` without proving independent sampling windows. It now requires
a strictly advancing acquisition counter between every adjacent record and
requires at least two snapshots with advancing cycles even if
`--min-records 1` is specified. Host regression covers replayed snapshots,
the single-record loophole, and a valid five-probe interval. This does not
verify physical probe reliability; T02-15 remains unchecked. Exact-commit
Exact-commit GitHub Actions `1bec44e72fc43928cc2d3d179e9a648aa113cfe0` (run 40655925832) **passed** native C++ host CTests, Python diagnostic tests, simulated WebSocket/API, headless LVGL, React checks and pinned ESP-IDF v6.1 firmware compilation.

## 2026-10-28 — per-observed-probe diagnostic rate sanity checks (software-only)

The physical qualification log analyzer now bounds cumulative valid/invalid
readings by `max_probes * cycles`, and each interval's readings by the
later sample's observed `max_probes * cycle_delta`, in addition to the
absolute five-probe cap. This catches impossible apparent throughput on a
one-probe bus rather than allowing it because firmware supports five.
New Python host regressions cover an impossible one-probe interval, impossible
cumulative count, and legitimate mid-window discovery of a second probe.
The report remains numeric-only and never includes raw serial data or
credentials. No actual physical readings were observed; `T02-15` remains
unchecked. Exact-commit CI `500e82fc7443f97eaf9650d37460371fc8c30e18`
(run 40653332038) **passed** the Python regression, native host,
simulated API/WebSocket, headless LVGL, React checks, and pinned
ESP-IDF v6.1 compilation. No physical hardware was accessed.

## 2026-10-28 — concurrent HTTP sessions cannot starve live WebSocket broadcasts (software-only)

Production `WsLive` previously used `httpd_get_client_list` with only six
entries, despite the separate four-WebSocket limit. More than six total
HTTP/asset/WS sessions could cause enumeration to fail and suppress all live
updates. Broadcasting now iterates only the four registered WebSocket slots;
each descriptor is checked against the HTTP server's current WebSocket type
before sending, avoiding writes to recycled REST connections. Existing session
authorization, slow-client close, bounded nonblocking sends, and work
coalescing remain in place. The native production `WsLive` POSIX-socket
test adds twelve unrelated HTTP sockets and requires healthy peers to receive
the next frame. This is deterministic host coverage, **not** ESP32 TCP
backpressure, Wi-Fi-client concurrency, or physical device acceptance.
`T07-10` stays unchecked pending real-device behavior. Exact-commit CI
`9b49917e3752c9bfd6b7a5268f3afd947905ba7e` (run 40652116700)
**passed** the production WebSocket host regression, simulated API/WebSocket,
headless LVGL, React checks and pinned ESP-IDF v6.1 firmware build.

## 2026-10-28 — physical-probe qualification report coverage (software-only)

The read-only `tools/sensor_hardware_report.py` analyzer previously could pass
`--expected-probes 5 --max-invalid 0 --max-faults 0` even when acquisition
cycles advanced but **no valid conversions** occurred. It now rejects zero
successful readings when physical probes are expected, checks that cumulative
and interval reading counts cannot exceed five readings per acquisition, and
rejects impossible six-probe counts. The optional
`--min-valid-per-cycle 5` requires an average of five valid conversions per
acquisition cycle over the sampled interval; use it with `--expected-probes 5`
for strict five-probe qualification. The JSON report exposes the numeric
`valid_delta` and configured threshold without printing raw logs or identities.

Python host regression covers absent conversions, incomplete five-probe
throughput, exact five-per-cycle success, impossible cumulative/interval
counts and no-cycle counter changes. This is a host-only evidence gate; no
physical bus, owner network, LCD, OTA, factory reset or LittleFS power
interruption was exercised. T02-15 remains unchecked. Exact-commit GitHub
CI `d9435b4c7c27b0aa73844d20dc9ab019a426c3a9` (run 40648388222)
**passed** native C++ host tests, Python diagnostic-report regression,
simulated API/WebSocket, headless LVGL, React checks and pinned ESP-IDF v6.1
firmware compilation. No device/hardware acceptance is implied.

## 2026-10-27 — diagnostic reporting hardening (software-only)

Exact-commit CI `6dc58704cfd2872adb46ef5bcd53fcdab6af6911`
(run 40641683891) **passed** C++ formatting, native host CTests including
the new production sensor diagnostics counters, Python report tests, simulated
WebSocket/API, headless LVGL, React checks, and pinned ESP-IDF v6.1 firmware
compilation. Prior formatting and host-header include errors were corrected.

The numeric-only report now enforces advancing acquisition cycles, validates
cumulative probe/fault/latency counter consistency, and applies its read-latency
threshold to the firmware's *cumulative maximum* rather than just the last
sampled duration. This catches a transient slow conversion even when it
occurred between serial log records. Added host regressions for the hidden
latency spike, stagnant cycles, and impossible counters. Exact-commit CI `d6a1ca947ab20647acabad69ca48bd821241ce24`
(run 40642278541) **passed** all pinned host, display, web, and ESP-IDF v6.1
checks, including the new numeric-report regressions. No hardware acceptance
is claimed. `T02-15` remains unchecked.

## 2026-10-27 — physical sensor instrumentation prepared (no hardware)

Added read-only `SensorDiagnostics` counters to the normal firmware acquisition
path. When the physical 1-Wire bus is explicitly enabled, it logs numeric-only
`SENSOR_DIAG` records once per health interval: cumulative acquisition cycles,
valid/invalid readings, shared-bus fault cycles, current/maximum probe count,
and last/maximum read latency. Counters are not emitted when the sensor bus is
disabled or for simulated readings. No ROM IDs, Wi-Fi settings, administrator
secrets or temperatures appear in this diagnostic log. Existing bare-board
safety defaults remain unchanged, including disabled GPIO4 and untouched BLK.

Added the non-destructive `tools/sensor_hardware_report.py` serial/log analyzer.
Live collection sets DTR/RTS low before opening, captures only recognized
numeric fields in memory, and optionally writes sanitized summary JSON. It
cannot flash, alter NVS, reset hardware or operate the LCD/backlight. Native
CTest uses the production counter implementation; Python unittest exercises
parsing, counters, threshold failures and secret-free output. Actual-device
qualification (physical five probes, pull-up, CRC reliability, scan latency,
retention, LCD electrical checks and long-run reliability) remains **open**.

To run locally **after** verifying physical wiring and deliberately enabling
`CONFIG_MONITOR_SENSOR_BUS` in a separate owner-managed build, use:
`python3 tools/sensor_hardware_report.py --port /dev/ttyACM0 --seconds 180 --expected-probes 5 --min-records 24 --max-invalid 0 --max-faults 0 --output .build/sensor-qualification.json`.
This reports numeric data but does not, by itself, prove final hardware
acceptance. Never attach an unverified LCD backlight to GPIO15.

## 2026-10-27 — repeated event-journal recovery state (software-only)

The event journal previously retained `last_event_` after a successful
`recover()` on an empty or newly recreated file. Reusing the same EventStore
instance could then silently suppress the first append as a duplicate, returning
success while writing no event. Recovery now builds the last-event state locally
and installs it only after full validation; successful empty/missing-file recovery
clears the old state. Failed recovery does not partially advance the sequence.

The native storage regression reuses one EventStore across truncate/recover,
unlink/recreate/recover, a damaged committed record, repair/recover, and
subsequent append; it checks physical file lengths and sequence continuity.
These are host/POSIX tests, not physical LittleFS or power-loss evidence.
T06-14/16 remain unchecked. Exact-commit CI `e0ed7d25` (run
40637029464) passed C++ formatting, native host storage/other CTests,
simulated API/WebSocket and headless LVGL checks, React tests/lint/typecheck/
production build, and pinned ESP-IDF v6.1 firmware compilation. This verifies
the software regression only, not physical LittleFS behavior. No board, credentials, reset, flashing, or OTA was used.

## 2026-10-27 — event journal validation-before-tail-trim (software-only)

The event-journal recovery code previously truncated a partial final append
before checking CRCs of the preceding committed records. On an otherwise
corrupt journal this silently modified evidence even though recovery returned
failure. Recovery now validates all complete event records and monotonic
sequences before altering the incomplete tail. A partial header indicating a
future `TME` journal format is also preserved instead of truncated.

The existing native storage suite now injects an invalid committed record
followed by a partial write, asserts that failed recovery leaves the entire
file intact, repairs the committed record, and verifies normal incomplete-tail
recovery. It also checks that a six-byte future-version prefix is retained
unchanged. These tests are POSIX simulations only; real LittleFS power
interruption and device-full acceptance remain outstanding under `T06-14/16`.
No physical devices, credentials, OTA, flash or reset were used.
Both changes passed exact-head CI: history `6daa43f7` (run 40628503271)

and event journal `eb6c5012` (run 40629360797), including native CTests,
simulated live clients, headless LVGL, React validation and pinned ESP-IDF v6.1
firmware compilation. The first event-journal CI stopped at clang-format; the
corrected exact commit passed. No device or physical power-loss evidence exists.

## 2026-10-27 — preserve complete CRC-damaged history records (software-only)

The history recovery path previously truncated an invalid final record without
checking whether its declared length had already been fully written. A complete
record with one damaged payload or CRC byte could therefore be silently erased
as if it were a power-cut prefix. Recovery now permits tail truncation only for
a recognized, structurally incomplete TMH1/TMH2 record. Complete invalid
records, unknown/malformed headers, and unsupported record lengths are retained
and cause recovery failure, preserving forensic data for safe diagnosis.

Expanded `history_power_cut_tests`: single-byte mutations of complete one-
and five-probe final records (excluding length-field ambiguity) must produce
an explicit failure and preserve the original damaged bytes. Existing exhaustive
prefix/rollover tests continue to require successful incomplete-tail recovery.
All cases are native/POSIX simulations, not LittleFS or hardware power-loss

acceptance. `T06-14` and `T06-16` stay unchecked. Exact-commit CI `6daa43f7` (run 40628503271) passed full native,
simulator, headless LVGL, React and pinned ESP-IDF v6.1 checks; no physical
device, owner network, OTA or reset used.

## 2026-10-27 — Wi-Fi scan policy verified on exact commit

Exact-commit GitHub Actions run 40625289515 for `bd7ecb3e` completed
successfully: the C++ formatting gate, all native host tests including
`wifi_scan_candidate_isolation`, simulated WebSocket/API integration,
headless LVGL regression, React lint/typecheck/tests/build, and pinned
ESP-IDF v6.1 firmware compilation passed. The preceding `10f69b00`
run failed only at the include-order formatting gate; the corrected
commit passed the complete workflow. This establishes software-only
regression coverage, not real Wi-Fi scanning/association or station DHCP.
`T05-04` remains unchecked pending owner-network/device verification.
CI: https://github.com/ekkus93/esp32-lcd-temperature-sensor/actions/runs/40625289515

## 2026-10-27 — exact-head CI formatting follow-up

The first exact-commit CI run for `10f69b00` stopped at clang-format
before host tests or ESP-IDF compilation. Its diagnostic points to the
Wi-Fi worker include order. This follow-up sorts the new header after
`wifi_credentials.hpp`; all software acceptance remains subject to the
new exact-head CI outcome. No device or owner credentials were used.

## 2026-10-27 — reject stale Wi-Fi scan timeout after denied scan (software-only)

The Wi-Fi worker previously armed a 10-second scan-stop timer before
checking whether `esp_wifi_scan_start` succeeded or whether candidate
association already occupied the radio. A rejected scan could therefore
trigger an unrelated `esp_wifi_scan_stop` later during station association.
The production worker now uses a host-testable `WifiScanWindow` policy:
only accepted scans arm the deadline, and completion/candidate takeover
cancel it. A new native CTest covers failed/denied scan starts, the exact
10-second boundary, one-shot expiry, cancellation, and successive scans.
The isolated C++17 host test passed with strict warnings enabled.
Full pinned ESP-IDF v6.1/React/LVGL/host CI remains the exact-head gate.
No RF, owner network, ESP32 board, LCD, probes, OTA, reset or flash was used.
`T05-04` stays unchecked pending owner-network/device handoff acceptance.

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

