## 2026-10-28 — Fail-closed storage append recovery (software-only)

The production history and event-journal writers now latch after a buffered
write, flush, fsync or close failure. A later append is rejected until a
successful recovery pass inspects and, where safe, repairs a torn tail.
The latch is cleared only by successful recovery, including empty/missing
journal initialization. Native storage regression injects ENOSPC using
`/dev/full`, restores the original persisted files, verifies retry remains
blocked before recovery, then verifies two intact history buckets and
continuous event sequences after recovery. This is a software-only
hardening step for T06-14; physical LittleFS power interruption (T06-16),
actual flash retention, OTA, owner Wi-Fi, LCD/probes and reset acceptance
remain open. Exact-commit CI verdict must be checked after this commit.
No hardware, credentials, flashing, OTA, reset or power cycling were used.

## 2026-10-28 — Dated project handoff and checklist audit

Created [ESP32_TEMPERATURE_MONITOR_HANDOFF_2026-10-28.md](ESP32_TEMPERATURE_MONITOR_HANDOFF_2026-10-28.md)
from exact `master` baseline `5d98b0e9ff0c570e52b5c24396bbf82457693a37` after verifying
GitHub Actions run 40779733269 passed. `docs/TODO.md` contains 114 checked
and 44 open checkboxes (158 total: 72.15%); numbered/working-agreement
progress is 114/145 (78.62%). Open boxes: 31 numbered implementation/device
acceptance tasks, 7 definition-of-done gates, 6 optional non-v1 blockers.
Last software integrity fixes are complete and CI-verified; this checkpoint
does not close hardware/owner-network/OTA/LittleFS/physical-reset acceptance.
No owner secrets or devices were accessed.

## 2026-10-28 — History UTC integrity and event transition preservation (software-only)

Two independent native-testable storage integrity fixes landed directly on master.
The TMH1/TMH2 history decoder now rejects CRC-valid records that contain a
nonzero UTC timestamp while the synchronized flag is clear, and rejects
negative UTC timestamps on both serialization and decoding. This prevents
silently accepting noncanonical persisted history and exposing invalid time.
Native storage regression recalculates valid CRCs for deliberately malformed
legacy and current records and verifies rejection; a valid synchronized
round-trip still succeeds. Exact-source CI `fca6d9612d351b93e456313b503c8a79522e8229`
(run 40777903610) **passed**.

EventStore previously suppressed any same-state/same-detail event within one
second even when the prior state or UTC synchronization status differed.
Duplicate suppression now also compares the prior state and optional UTC.
Native storage tests verify exact duplicates remain coalesced, while distinct
previous-state transitions and newly synchronized UTC events are persisted
with continuous sequence numbers across reopen. Exact-source CI
`7c758ada5202c1ad21586a5fc5064ec97fa1d9da` (run 40778698147)
**passed**. Both workflows include native C++, Python diagnostics, simulated
REST/WebSocket, headless LVGL, React verification, and pinned ESP-IDF v6.1
firmware compilation. These are host and simulated tests, not evidence of
physical LittleFS durability. T06-14/T06-16, hardware/owner Wi-Fi, OTA,
factory reset and sustained combined-load acceptance remain unchecked.
No devices, credentials, flashing, OTA, power cycling or reset were used.

## 2026-10-28 — Preserve short future history-format tails (software-only)

HistoryStore recovery previously checked unknown TMH format/version headers
only after six bytes were present. An interrupted four- or five-byte TMH3
header (or TMH2 with an incompatible fifth version byte) was therefore
truncated as an ordinary incomplete TMH2 write. Recovery now checks the known
magic at four bytes, schema version at five, and the upper version byte at
six, refusing recovery without modifying the journal when a future format
is identified. Existing genuine TMH1/TMH2 interrupted-write recovery is
preserved. Native `history_power_cut_recovery` tests add short unknown
headers appended after a valid persisted bucket and assert byte-for-byte
preservation; existing tests exhaustively cover all partial TMH2 byte
boundaries for one and five probes, including segment rollover. Initial implementation `ea71e085` failed C++ formatting (CI run
40772106565); corrected commit `5c35b6be5f2b360c5e46be6dead10bd24f792977`
passed exact-commit CI run 40772204709, including native C++ storage/
power-cut tests, Python diagnostics, simulated REST/WebSocket, headless LVGL,
React checks and pinned ESP-IDF v6.1 firmware compilation.
This is **host/POSIX simulation only**; physical LittleFS interruption,
OTA rollback, and hardware acceptance remain open (T06-14/16, T10-09).
No devices, credentials, OTA, flashing, reset or other hardware were used.

## 2026-10-28 — late Wi-Fi scan completion releases scan ownership (software-only)

A scan-done event arriving at or after the ten-second scan deadline could
consume `WifiScanWindow`'s deadline and skip the worker's periodic timeout
cleanup. The `scanning_` flag then remained true indefinitely, rejecting
future scan requests. `WifiScanWindow::completed()` now returns distinct
accepted/ignored/timed-out outcomes. The production worker discards the
stale AP list and clears `scanning_` immediately on a timed-out completion;
canceled or duplicate events remain ignored without changing candidate state.

Native C++17 scan-policy regression verifies all three completion outcomes,
the exact deadline boundary, repeated events and cancellation. Implementation
commit `b1bafe7555a0c1ea867527531c8b2dc26a90aa98` passed exact-commit
GitHub Actions run `40760479694`: native C++/Python, simulated REST/WebSocket,
headless LVGL, React verification and pinned ESP-IDF v6.1 compilation.
This is host-only coverage, not real ESP32 radio/event-ordering or owner Wi-Fi
qualification. T05-04/T05-18 remain open, as do physical DS18B20/LCD,
LittleFS power interruption, OTA success/rollback, disposable factory reset
and sustained-load acceptance. No owner credentials, hardware, flashing,
reset or OTA were used.

## 2026-10-28 — ignore canceled/late Wi-Fi scan completion events (host-only)

The production Wi-Fi worker previously processed every queued
`WIFI_EVENT_SCAN_DONE`, including events arriving after a scan timed out
or was canceled for candidate station association. A late completion could
replace the previous network list and clear scan state during a candidate
handoff. `WifiScanWindow::completed(now_ms)` now accepts only an active
scan that completed before its ten-second deadline; the worker ignores
late/unowned completion events without reading scan records. The worker
reads current monotonic time at completion rather than relying on the
timestamp captured before its queue wait.

Native C++17 scan-policy regression passed locally with
`g++ -std=c++17 -Wall -Wextra -Werror -Wpedantic`; it covers valid
completion, duplicate completion, candidate cancellation, rejected scans,
timeout, and a completion at the deadline. Exact-SHA implementation
`51c31210fed28abcd1a98d47ac010140e3faf9db` **passed** GitHub
Actions run `40754183030`, including native C++/Python, simulated
REST/WebSocket, headless LVGL, React checks and pinned ESP-IDF v6.1
compilation. This does not prove actual ESP-IDF Wi-Fi event ordering,
owner-network DHCP/scan behavior or phone onboarding. T05-04 and T05-18
remain unchecked. No owner hardware, credentials, flashing, OTA, reset
or physical LittleFS power loss was used.

## 2026-10-28 — preserve BOOT provisioning window across candidate DHCP (host-only)

The production `WifiApWindow` previously reused one AP-close deadline for
owner-requested BOOT recovery (10 minutes) and successful Wi-Fi candidate
handoff (30 seconds). Starting or completing a candidate during manual
recovery could silently replace the ten-minute owner window and close the
provisioning AP after only 30 seconds. The policy now tracks both deadlines
independently and closes only when both have elapsed, the candidate is idle,
and station connectivity is present. After a handoff disconnect, a fresh
30-second grace is required on reconnection even if manual recovery expired.

Native C++17 policy regression passed locally with
`g++ -std=c++17 -Wall -Wextra -Werror -Wpedantic`, including BOOT before,
during and after candidate success, failed/retried candidates, disconnected
handoff, and a long offline interval. Exact-SHA implementation commit
`862078e43cd34bdab56afbd1add4d2d65a027c8a` **passed** GitHub
Actions run `40752450935`: native C++/Python tests, simulated
REST/WebSocket, headless LVGL, React checks, and pinned ESP-IDF v6.1
compilation. This is software-only timing evidence. Physical BOOT button,
owner-network DHCP/reconnect, phone onboarding, and AP-to-STA acceptance
remain open under T05-04/05/10/11/18. No owner hardware, Wi-Fi/admin
credentials, flashing, OTA, factory reset or physical power loss was used.

## 2026-10-28 — retire vanished WebSocket registrations (host-only)

Production `WsLive::broadcast` now clears a subscriber slot when the ESP-IDF
HTTP server no longer reports its descriptor as a WebSocket. Previously that
stale registration could survive a broadcast, retaining its old token and
potentially sending a snapshot if the same descriptor later reappeared in
the HTTPD WebSocket registry without a new `add_client` admission.
A native POSIX-socket/HTTP-shim regression removes a registered descriptor
from the server registry, broadcasts, simulates descriptor reuse, confirms
no unsolicited frames, then confirms delivery only after authenticated
readmission. Two earlier CI attempts (`70931d28`, `c3b399c4`) failed the
clang-format gate on an inline test comment; corrected exact-SHA
`66d1ace2961e483c6b8442a96ea8d2e0b85addbc` **passed** GitHub Actions
`40733956707`: native C++/Python host tests, simulated REST/WebSocket,
headless LVGL, React checks and pinned ESP-IDF v6.1 compilation.
This is host-only evidence, not proof of ESP32 fd-reuse or TCP behavior;
T07-10/T10-05 remain unchecked. No hardware, owner credentials, flashing,
OTA, factory reset or physical LittleFS interruption was used.

## 2026-10-28 — skip live snapshot allocation without authorized subscribers (host-only)

The production WebSocket broadcaster previously built a complete JSON snapshot
on every sampling broadcast even with zero clients, or only expired/quarantined
clients. It now lazily calls the snapshot producer at most once per broadcast,
only after a live authorized subscriber is found. Expired-session eviction and
failed-close quarantine maintenance continue even without a snapshot. Native
production HTTP-server-shim/POSIX-socket regression verifies zero producer calls
with no subscribers or only an expired session, and one shared producer call
for two healthy peers receiving complete frames. Exact commit
`d87775554786e120a5463c962221505f1cef45de` passed GitHub Actions run
`40724203719`: native C++/Python, simulated REST/WebSocket, headless LVGL,
React and pinned ESP-IDF v6.1 compilation. This reduces avoidable JSON/heap
work on the memory-constrained device but does not establish measured ESP32
heap floors, Wi-Fi/TCP backpressure, or concurrent device behavior. Keep
T07-10 and T10-05 unchecked. No hardware, owner Wi-Fi/admin credentials,
flashing, OTA, factory reset or physical LittleFS power loss were used.

## 2026-10-28 — revalidate administrator sessions at WebSocket admission (host-only)

The HTTP WebSocket pre-handshake checks the administrator session, but an
already-expired/revoked token could previously be accepted by WsLive::add_client
if it expired between pre-handshake and slot registration. Admission now
revalidates the token before allocating or overwriting a client slot. The
production broadcaster still evicts tokens that expire *after* admission,
including when snapshots are empty. Native HTTP-shim/POSIX-socket regression
now uses a simulated clock to cover rejection at admission, later expiry,
healthy-client isolation, and resumed delivery. No owner device, network
credentials, flash, OTA, reset or physical power interruption was used.
Initial commit `4f09b9ec` failed only the clang-format gate on the host authorization mock (run `40715234147`). Corrected commit `343a86f5e89ac1e899798c6ec1d4a08e4ff59882` **passed** exact-SHA GitHub Actions run `40715720284`: C++ formatting, native C++/Python host tests, simulated REST/WebSocket, headless LVGL, React checks, and pinned ESP-IDF v6.1 firmware compilation. The historical tail of this progress file, accidentally omitted by the preceding documentation-only commit, was restored.
T07-10 remains unchecked for actual ESP32 TCP backpressure/concurrency.

## 2026-10-28 — maintain WebSocket cleanup with empty snapshots (host-only)

The production live broadcaster previously returned immediately on an empty snapshot, indefinitely postponing expired-token eviction and retries to close quarantined WebSocket streams whenever the producer had no data. It now continues session maintenance while suppressing empty data frames. Native HTTP-server/POSIX-socket tests cover both expired-token eviction and successful retry of a previously refused quarantine close with empty snapshots, plus isolation of healthy clients and resumed delivery after data returns. Exact-commit `70d228d3d517a3fb8f5d1bebda4680a32b018178` **passed** GitHub Actions run `40702928583`: 18 native CTests, Python diagnostics, simulated REST/WebSocket, headless LVGL, React checks, and pinned ESP-IDF v6.1 firmware compilation. T07-10 remains unchecked pending real ESP32 TCP slow-client, concurrency, and network-recovery acceptance. No hardware was accessed or flashed; no owner Wi-Fi/admin secrets, OTA, factory reset, or physical LittleFS interruption were used.

## 2026-10-28 — quarantine failed WebSocket sends when close is refused (host-only)

The production broadcaster now marks a live socket as quarantined after a failed nonblocking RFC 6455 send. If the HTTP server refuses the requested close, subsequent broadcasts retry closing but never send another frame into a possibly partial stream; admission also refuses reusing that descriptor until closure. A native POSIX-socket regression saturates one peer, injects close refusal, drains its TCP backlog, checks that no new frame appears, then verifies eventual close while a healthy peer continues receiving. The first attempt `e8d22c9a` failed clang-format; `06bf7428` exposed an existing test that expected reuse of a closed slot. The implementation now clears a quarantined slot after confirmed close. Exact-commit `26b5d23e960a5ecc30e47b35c3bf87519b1ae920` **passed** GitHub Actions run `40694536202`: all 18 native CTests, Python diagnostics, simulated REST/WebSocket, headless LVGL, React lint/tests/build, and pinned ESP-IDF v6.1 firmware compilation. T07-10 remains unchecked for real-device backpressure/concurrency. No hardware, credentials, flashing, OTA, reset or physical LittleFS interruption was used.

## 2026-10-28 — fail closed on WebSocket forbidden-frame close errors (software-only)

The live WebSocket endpoint rejects all client data frames. Previously, if the HTTP server refused the requested socket close for a forbidden frame, the handler still returned ESP_OK, potentially leaving a disallowed connection accepted. It now returns ESP_FAIL on that close failure so the HTTP server can terminate the request. Native HTTP-server-shim coverage injects a close failure for an empty client text frame and verifies the handler fails without claiming the close succeeded. Exact-commit GitHub Actions `a034505fb7972bfe29c2994cb7fdc1301fc6e801` (run 40685306378) **passed** the native C++ host suite, Python diagnostics, simulated API/WebSocket, headless LVGL, React checks and pinned ESP-IDF v6.1 firmware compilation. A follow-up native test also injects a failed expired-session eviction during broadcast and verifies the expired subscriber receives no snapshot while healthy subscribers continue; Exact-commit CI `593dcf928841101492ae25e7335358136138136a` (run 40685901007) **passed** all native host tests, Python diagnostics, simulated REST/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1 firmware compilation. T07-10 remains unchecked for real-device concurrent/slow-client and network evidence. No device, Wi-Fi credentials, flashing, OTA, destructive reset or power interruption was used.

## 2026-10-28 — reconcile WebSocket admission test after expired-session reclamation (software-only)

Exact-commit CI `949a8871ee4a9292d4dbaea9d42c56342c4acc1d` (run 40679993045) **failed** `websocket_live_client_isolation`: the old test expected a fifth WebSocket to be rejected even while one of four registered sessions was already expired. Production admission deliberately reclaims expired slots. The native POSIX/HTTP-shim regression now first fills four valid slots and asserts fifth-client rejection, then expires a registered token to test broadcast-time eviction. The separate admission-time reclamation regression remains intact. Commit `63272fc4b2be62445d650d3d30dcc40cca73622f` **passed** exact-commit CI run 40681140003, including 18 native CTests, Python diagnostics, simulated REST/WebSocket, headless LVGL, React checks and pinned ESP-IDF v6.1 firmware compilation. No hardware was accessed or flashed, and no owner credentials, OTA, destructive reset or physical power-loss tests were performed. T07-10 remains open for real-device slow-client, eviction and concurrency evidence.

## 2026-10-28 — reclaim expired WebSocket sessions at admission (software-only)

The production four-client live WebSocket table could reject a valid new
administrator connection when an expired/logged-out session still occupied
a slot between periodic broadcasts. Admission now closes one unauthorized
session and reuses its slot only when no free/stale descriptor exists.
A native POSIX/HTTP-server-shim regression fills four valid slots, confirms
fifth-client rejection, expires one token, and verifies immediate admission
without closing healthy subscribers. CI verification is pending. This does
not establish real ESP32 TCP backpressure, Wi-Fi concurrency, or session
eviction latency; T07-10 remains unchecked. No hardware, secrets, flashing,
OTA, reset or physical filesystem tests were used.

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

