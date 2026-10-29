## 2026-10-29 — Event journal path substitution and compaction guard

The journal now refuses symlink/non-regular path substitutions after
recovery for writes and API pages. Compaction exclusively creates .tmp
rather than truncating a pre-existing path. Native tests cover target
preservation, writer latch and sequence continuity after recovery.
Exact-source `560f998a` passed GitHub Actions `40836865635` (native C++/Python,
simulated API/WebSocket, headless LVGL, React, pinned ESP-IDF v6.1).
Physical LittleFS/power-loss,
owner network, sensors/LCD, OTA and reset gates remain open.

## 2026-10-29 — History segment path hardening verified in exact-head CI

HistoryStore now refuses symlink/non-regular segment paths before recovery,
export, and append, and fails closed on directory/read errors. Native
regression preserves external segment bytes, refuses dangling links/directories,
and uses regular-file RLIMIT_FSIZE write/flush failure injection without
bypassing the new path checks. Initial `b6c89553` failed formatting
(Actions `40834070502`); `93c1f4d0` passed formatting and ESP-IDF build
but exposed the obsolete /dev/full history symlink test (Actions
`40834400874`). Corrected source `cf6e9c8c` **passed** exact-SHA Actions
`40835122250`, including native C++/Python, simulated API/WebSocket,
headless LVGL, React and pinned ESP-IDF v6.1. Physical LittleFS
power interruption, sensor/LCD, owner Wi-Fi, OTA, factory reset and
sustained-load acceptance remain open. No secrets or devices accessed.

## 2026-10-29 — History segment path and read-failure integrity (software-only)

HistoryStore now refuses symbolic-link and non-regular segment paths on
native hosts during recovery, export and append, including a segment
replaced after successful recovery. It also fails closed when directory
enumeration or record/tail stream reads fail, rather than assuming an empty
directory or recoverable interrupted append. ESP-IDF v6.1 uses stat() for
its LittleFS VFS (which does not support symlinks); host regression uses
lstat() to reject links without following them. Native storage regression
covers an existing symlink to a preserved segment, a dangling symlink,
a directory segment, and recovery after removing invalid paths.

Exact-head GitHub Actions verification pending for this commit. This is
software-only protection, not real ESP32 LittleFS power interruption,
wear measurement, OTA/rollback, owner-network, LCD/probe, physical reset or
sustained-load acceptance. T06-14/T06-16 and all owner/hardware gates
remain unchecked. No credentials or physical devices were accessed.

## 2026-10-28 — Journal I/O failure handling and link preservation

Previous platform-conditional recovery fix `2ceca9d0` passed full exact-head
CI run `40827973869`. The next source change fails closed on journal
read errors during recovery, compaction and pagination, with a host regression
ensuring an existing symlink target is preserved. Corrected exact-head commit `6d41629f811343ab4f30832428ca617f05f08941`
passed full CI run `40828571625` (native/Python, simulated API/WebSocket,
headless LVGL, React, pinned ESP-IDF v6.1).
Hardware/owner acceptance unchanged; no devices, credentials, flashing or
destructive actions used.

## 2026-10-28 — ESP-IDF event-store VFS compatibility correction

The previous event-journal path-safety commit `3a3323f5` failed pinned
ESP-IDF v6.1 linking due to unavailable `lstat` (Actions `40823488328`).
Use ESP-IDF VFS `stat` on device, retaining host `lstat` for symlink
regressions. The corrected exact-head CI is pending; all physical/owner
acceptance remains open. No hardware or credentials were accessed.

## 2026-10-28 — Safe event-journal file recovery (software-only)

Recovered journals are no longer at risk of being truncated when a pre-existing
path cannot be read: lstat rejects symlink/non-regular paths, and initial
creation requires an exclusive, owner-only file before enabling append.
Native regression covers dangling symlink, directory refusal and normal
creation/recovery. Exact-head CI pending; T06-14/T06-16 and owner/hardware
acceptance remain open. No hardware, credentials, flashing, OTA, destructive
reset or power interruption were used.

## 2026-10-28 — Storage writers require explicit successful recovery

HistoryStore and EventStore now initialize append permission to false, so
a new instance cannot write using stale/default segment or event-sequence
state before recovery. Native tests cover empty and populated stores without
mutation and normal writes after recovery. Exact-source commit `b6584979`
passed Actions run `40811896227` (native/Python, simulated REST/WebSocket,
headless LVGL, React, pinned ESP-IDF v6.1). Earlier malformed partial event
and history header fixes also passed exact-SHA CI `40810808184` and
`40811329826`. No physical LittleFS/power-loss, reboot, Wi-Fi, LCD/probes,
OTA, factory reset or sustained-load acceptance was performed. TODO remains
114/158 checked; T06-14/T06-16 and owner/hardware gates stay open.

## 2026-10-28 — Conservative partial storage header recovery (software-only)

EventStore now preserves malformed 1–8-byte TME1 header prefixes rather than
truncating them as interrupted writes; corrected source `89e9c101` passed
exact-commit CI run `40810808184` after a formatting-only initial failure.
HistoryStore now likewise preserves malformed short TMH1/TMH2 headers and
impossible record lengths; source `0510da53` passed exact-commit CI run
`40811329826`. Native tests prove byte preservation and retain exhaustive
valid interrupted-append recovery; both CI runs include simulated REST/
WebSocket, headless LVGL, React and pinned ESP-IDF v6.1. This is host-only
recovery hardening, not evidence of physical LittleFS power-loss, reboot,
owner-network, probes/LCD, OTA/rollback, factory reset or sustained load.
T06-14/T06-16 and other hardware acceptance remain unchecked. Canonical
TODO remains 114/158 checked (72.15%). No owner devices or credentials
were accessed.

## 2026-10-28 — Oversized event journal refusal (software-only)

Event pagination and compaction now refuse a journal exceeding its configured
maximum even when all records are individually CRC-valid. Native tests confirm
no records are silently removed, recovery fails closed, and successful
host-only repair/recovery restores valid append sequencing. Implementation
`6e05d14b` passed exact-source Actions run `40806004585` (native C++/Python,
simulated REST/WebSocket, headless LVGL, React, pinned ESP-IDF v6.1).
T06-14 remains unchecked; physical LittleFS/power-loss, owner Wi-Fi,
LCD/probes, OTA/rollback, reset and sustained-load gates remain open.
No hardware or secrets were accessed. TODO remains 114/158 checked.

## 2026-10-28 — Storage writer recovery latch (software-only)

A failed re-recovery no longer leaves HistoryStore/EventStore append-enabled
from a prior successful recovery. Failed event compaction also blocks further
appends until successful revalidation. Native regression exercises both reused
writers and repaired-but-not-recovered files; exact-source commit `27e00f04`
passed Actions run `40804958259` (native C++/Python, simulated API/WebSocket,
headless LVGL, React, pinned ESP-IDF v6.1). T06-14 and all physical LittleFS,
owner-network, LCD/probe, OTA, factory-reset and sustained-load gates remain
open. TODO still 114/158 checked; no hardware or secrets accessed.

## 2026-10-28 — Event-journal pagination integrity (software-only)

The bounded event-journal pagination reader now validates every record beyond
the requested page boundary before marking the page available. Native regression
covers a valid three-event one-item page, CRC-valid duplicate sequence in a
later record, and a torn tail; invalid journals fail closed. Exact-source
`abafc225ec6599c88a4113fe258bba054215ad5c` passed GitHub Actions
run 40798587386 (native C++/Python, simulated API/WebSocket, headless LVGL,
React and pinned ESP-IDF v6.1). T06-14 remains open for actual LittleFS
power-loss/reboot acceptance. All owner-network, sensor, LCD, OTA, destructive
reset and combined-load hardware gates remain open. No device or credentials
were accessed.

## 2026-10-28 — Dated project status checkpoint

The comprehensive date-stamped handoff is in
[ESP32_TEMPERATURE_MONITOR_HANDOFF_2026-10-28.md](ESP32_TEMPERATURE_MONITOR_HANDOFF_2026-10-28.md).
At baseline `5d98b0e9ff0c570e52b5c24396bbf82457693a37`, exact-commit CI 40779733269
passed. The canonical TODO totals 114 checked / 44 open = 72.15% of all
158 checkboxes, or 114/145 = 78.62% for working-agreement + numbered
implementation tasks (31 numbered tasks still open). Seven definition-of-done
and six nonblocking optional items account for the other 13 open boxes.
Physical hardware, owner-network, OTA, reset, power interruption and soak
acceptance remain open. No physical device or credential operation occurred.

## 2026-10-28 — Host-only storage integrity follow-up

History TMH1/TMH2 now rejects noncanonical unsynchronized timestamps and
negative UTC values; CRC-recomputed malformed-record regressions pass.
EventStore now preserves state-transition and UTC-sync changes inside the
one-second duplicate window while still coalescing exact duplicates.
Exact-commit CI passed for `fca6d961` (run 40777903610) and
`7c758ada` (run 40778698147), including native C++, Python,
simulated REST/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1.
T06-14/T06-16 and all physical LittleFS/power-loss, hardware,
owner-network, OTA and destructive-reset acceptance remain open.
No owner devices or secrets were accessed.

## 2026-10-28 — Short future-format history headers (software-only)

History recovery now conservatively preserves unknown TMH/version prefixes
at four, five and six bytes instead of deleting future-format evidence on
a rollback. The native host power-cut suite covers byte-for-byte preservation
and existing genuine TMH2 partial records. Exact-commit CI `5c35b6be` (run 40772204709) passed the native host power-cut regression, Python, simulated API/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1 build.
Physical LittleFS power-loss/recovery, OTA, hardware and owner network
acceptance remain open; no device operations were performed.

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

## 2026-10-28 — stale Wi-Fi scan completion isolation (software-only)

The Wi-Fi worker now rejects `WIFI_EVENT_SCAN_DONE` after a canceled,
timed-out, rejected or already completed scan, rather than publishing stale
scan results or changing state during candidate association. The production
scan policy and native C++17 regression passed locally, and exact-SHA
`51c31210` passed GitHub Actions `40754183030` (native/Python,
simulated API/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1).
Physical scan/candidate handoff and phone/DHCP onboarding remain open under
T05-04/T05-18. All hardware, LittleFS interruption, OTA/rollback, owner
Wi-Fi, factory-reset and long-soak acceptance remains deferred. No owner
device, secrets, flashing, OTA or reset were used.

## 2026-10-28 — independent BOOT recovery and candidate AP deadlines (software-only)

The Wi-Fi AP policy now preserves an owner-initiated ten-minute BOOT recovery
window even when a candidate starts, succeeds, fails, or retries during that
window. A separate 30-second candidate handoff grace is refreshed after
station reconnect. Native C++17 timing regression passed locally, and exact
implementation SHA `862078e4` passed GitHub Actions `40752450935`
(native/Python, simulated REST/WebSocket, headless LVGL, React and pinned
ESP-IDF v6.1). T05-04/05/10/11/18 remain open for physical BOOT behavior,
owner Wi-Fi/DHCP, AP-to-STA and phone onboarding. The LCD/backlight, five
DS18B20 probes, LittleFS power loss, OTA/rollback, destructive factory
reset and sustained-load acceptance still need owner hardware. No device
or credentials were accessed and no flash/reset/OTA was performed.

## 2026-10-28 — AP-to-STA handoff disconnect recovery (software-only)

A candidate that obtained DHCP could disconnect during the 30-second AP
handoff grace, yet the old deadline would close the setup AP while offline.
The Wi-Fi worker now cancels that candidate-close deadline on disconnect,
rearms 30 seconds after reconnect, and only auto-closes the AP when station
connectivity is present. BOOT manual recovery remains independent of the
candidate grace period. Native policy regression passed locally; after one
formatting-only CI failure, exact-SHA `f5d1a25c` passed Actions run
40743623669 (native/Python, simulated API/WebSocket, headless LVGL, React,
pinned ESP-IDF v6.1). T05-04/05/11/18 remain open for owner-network DHCP,
reconnect and phone acceptance. All physical sensor/LCD, LittleFS power-loss,
OTA success/rollback, destructive reset and soak gates remain deferred. No
hardware, credentials, flashing, OTA, reset or power-loss tests were used.

## 2026-10-28 — stale WebSocket descriptor reclamation (software-only)

The production broadcaster now retires registrations whose fd is no longer
recognized as a WebSocket by HTTPD, preventing an old subscriber token from
surviving an observed disconnect and authorizing frames after simulated fd
reuse. Native POSIX-socket/HTTPD-shim tests require fresh authorized admission
before delivery. Two formatting-only CI failures were corrected; exact-SHA
`66d1ace2` passed Actions run `40733956707` with native/Python,
simulated API/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1.
T07-10/T10-05 and all hardware/owner-network/OTA/reset/LittleFS power-loss
acceptance remain open. No device or secrets were used.

## 2026-10-28 — lazy WebSocket snapshots (software-only)

Production WsLive now avoids constructing snapshot JSON without an authorized
live subscriber while continuing expiry/quarantine cleanup. Host POSIX socket
regression covers zero subscribers, expired-only subscribers, and two healthy
subscribers sharing one producer call. Exact-SHA commit `d8777555` passed
GitHub Actions run `40724203719` (native/Python, simulated API/WebSocket,
headless LVGL, React, pinned ESP-IDF v6.1). T07-10/T10-05 remain open for
physical ESP32 TCP/backpressure and sustained heap-floor evidence. All other
physical sensor/LCD, owner Wi-Fi/DHCP/NTP, LittleFS interruption, OTA success/
rollback, and disposable-device factory reset acceptance remains deferred.
No owner device, credentials, flash, reset or OTA operations were used.

## 2026-10-28 — WebSocket admission authorization recheck

Production WsLive now revalidates the administrator session at registration, preventing an expired/revoked token from occupying a live slot between HTTP pre-handshake and registration. The native HTTP/POSIX-socket shim uses simulated expiry to cover rejected admission and valid sessions that expire after registration. Initial commit `4f09b9ec` failed clang-format on the host mock; corrected exact-SHA commit `343a86f5` passed GitHub Actions `40715720284` (native/Python, simulator, headless LVGL, React and pinned ESP-IDF v6.1). T07-10 stays open for actual ESP32 TCP slow-client/concurrent-session acceptance. No hardware, credentials, flash, OTA, reset or physical power-loss operations occurred.

## 2026-10-28 — empty-snapshot WebSocket session maintenance

Commit `70d228d3d517a3fb8f5d1bebda4680a32b018178` passed exact-SHA GitHub Actions `40702928583` (native C++/Python, simulated REST/WebSocket, headless LVGL, React, ESP-IDF v6.1). The live broadcaster now evicts expired sessions and retries closing quarantined streams even when snapshot production yields no data; healthy clients receive no empty data frames and resume updates normally. Host socket-pair regression verifies both paths. T07-10 still needs physical ESP32 TCP/backpressure/concurrent-client acceptance. Other deferred owner prerequisites: physical five-probe/1-Wire and LCD electrical qualification, owner Wi-Fi association/DHCP/NTP/phone onboarding, LittleFS power interruption, OTA success/rollback, and disposable-device factory-reset acceptance. No hardware, credentials, flashing, destructive reset, or OTA were used.

## 2026-10-28 — software-only WebSocket failed-send quarantine

A failed nonblocking WebSocket send now quarantines its client even when the HTTP server refuses to close it. No further frames are sent on the possibly corrupted stream; closure is retried. Host POSIX socket-pair coverage exercises backpressure, refused close, drained socket, eventual close and unaffected healthy subscriber. Exact-commit `26b5d23e` passed GitHub Actions run `40694536202` with all 18 native CTests, Python, simulated API/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1 firmware compilation. The quarantined slot is cleared only after confirmed HTTP-server close. T07-10 still requires physical ESP32 network/backpressure acceptance; no device, owner Wi-Fi, flash, OTA, destructive reset or power-loss work was performed.

## 2026-10-28 software-only WebSocket close-error verification

The read-only live endpoint now fails its handler if a forbidden client data frame cannot be closed by the HTTP server; an expired WebSocket whose close is refused is never sent snapshots, and healthy subscribers continue. Native HTTP-server/POSIX-socket shim regressions cover both failures. Exact-commit GitHub Actions passed for implementation `a034505fb7972bfe29c2994cb7fdc1301fc6e801` (run 40685306378) and follow-up regression `593dcf928841101492ae25e7335358136138136a` (run 40685901007): native C++, Python diagnostics, simulated REST/WebSocket, headless LVGL, React, and pinned ESP-IDF v6.1 compilation. T07-10 stays open for real-device slow-client/concurrent-network acceptance; no device, secrets, flashing, reset, OTA or physical power-loss work was done.

# Project handoff — 2026-10-27

## Current state

### 2026-10-28 — WebSocket admission regression reconciled

Exact-head `949a8871` CI (run 40679993045) failed one native WebSocket test because its four-slot cap expectation contradicted new expired-session reclamation. Host test commit `63272fc4` makes all four initial slots valid before checking rejection, then expires a token for the existing broadcast-eviction test. Exact-SHA CI `63272fc4` (run 40681140003) **passed** native C++/Python, simulated REST/WebSocket, headless LVGL, React and ESP-IDF v6.1 build. T07-10 and hardware/owner-network/LittleFS power-loss/OTA/reset acceptance stay unchecked. No physical devices or credentials were used.


### 2026-10-28 event journal interrupted-append regression

Exact-commit CI `98df8e1e` (run 40678870619) passed after reconciling
formatting and a regression-test expectation. The native test now covers
all 60 interrupted TME1 append prefixes and recovery/sequence continuity;
firmware recovery preserves incomplete future-version headers from the
first incompatible byte. All 18 native CTests, simulated API/WebSocket,
headless LVGL, React and pinned ESP-IDF v6.1 compilation passed. T06-14/16
remain open for physical LittleFS/power-loss and device concurrency checks.
No owner device, Wi-Fi credentials, flash, OTA or reset was used.

### 2026-10-28 damaged diagnostic log evidence

The sensor qualification analyzer now fails closed when a `SENSOR_DIAG`-marked
line is malformed instead of silently skipping it. It emits only a numeric
`malformed_records` count. Host regression and exact-commit CI `9ec3ff3c` (run 40670068891)
passed the full native/Python, simulator, headless LVGL, React and pinned
ESP-IDF v6.1 workflow.
This does not close physical T02-15 or any owner-device gate.


### 2026-10-28 diagnostic-evidence integrity

Exact-commit CI `f36e8839` (run 40661726628) **passed** the native,
Python, simulator, headless LVGL, React and pinned ESP-IDF v6.1 gates.
The read-only physical sensor report now rejects ambiguous/duplicated UART
fields and impossible interval fault-cycle growth. These are host-only
integrity checks, not physical five-probe qualification. `T02-15` and all
other hardware/owner-network/OTA/reset/LittleFS power-loss gates remain open.
No owner device was touched.

### 2026-10-28 software-only verification

The latest exact-commit CI `1bec44e7` (run 40655925832) **passed** native host/Python, simulator, headless LVGL, React and pinned ESP-IDF v6.1 checks. The read-only sensor qualification report now rejects duplicated `SENSOR_DIAG` records and one-record reports even with a relaxed `--min-records` setting. This is host-only and does not close T02-15 or authorize attaching probes/backlight. No owner Wi-Fi credentials, physical reset, successful OTA, or LittleFS power-loss evidence was used.

Two commits on `master` passed their own exact-commit GitHub Actions runs:
`9b49917e` (40652116700) removes a six-fd HTTP enumeration bottleneck
from the four-client live WebSocket broadcaster; `500e82fc`
(40653332038) tightens read-only numeric sensor report bounds to the
largest probe population actually observed. Native C++, Python, simulator,
headless LVGL, React and pinned ESP-IDF v6.1 checks passed on the
respective commits. The real-device WebSocket backpressure gate `T07-10`
and actual probe/bus gate `T02-15` stay open; neither host test
constitutes physical ESP32, Wi-Fi, LCD, OTA, reset or LittleFS power-loss
acceptance. Do not enable the unverified backlight or flash/reset owner devices.


### Physical sensor qualification preparation — 2026-10-27

Firmware now has read-only SENSOR_DIAG aggregate counters and a safe local serial/log analyzer at `tools/sensor_hardware_report.py`. They are host-testable and emit no sensor ROMs or credentials. A physical DS18B20 bus still requires deliberate `CONFIG_MONITOR_SENSOR_BUS` enablement after wiring and 4.7 kΩ pull-up checks. Do not connect the LCD backlight to GPIO15 without electrical sign-off. All hardware/owner-network/OTA/power-loss/reset acceptance remains unchecked. Exact-commit CI `6dc58704` (run 40641683891) passed the full host/display/web/ESP-IDF v6.1 workflow. The follow-up hardens the numeric report against hidden read-latency spikes and stagnant counters; exact-commit CI `d6a1ca94` (run 40642278541) passed. See `docs/PROGRESS.md` for the test command. This does not establish physical hardware acceptance.


The latest software-only change isolates the Wi-Fi scan timeout from failed
or denied scan requests. Only accepted scans arm a ten-second deadline;
scan completion and candidate takeover cancel it. Native CTest verifies the
policy; exact-head CI passed for `bd7ecb3e` (run 40625289515), including
pinned ESP-IDF v6.1 firmware and the full host/display/web workflow. No RF, owner
credentials or hardware acceptance is claimed. T05-04 remains open.

### Latest software-only checkpoint — 2026-10-27

On master, the station NVS credential codec now preserves valid legacy 99-byte
records and writes 103-byte CRC32-checked records only after candidate
connection success. Invalid records are rejected without erasure. The
independent host regression covers 103 byte mutations, truncation, invalid
bounds and legacy compatibility. The OTA pending-verify path now requires two
acquisition/publish cycles **and** successful history worker, Wi-Fi stack,
authentication, HTTP server and sampling initialization before validating the
new image. Missing probes/LCD, router connectivity and unsynchronized NTP
remain permitted offline operating states.

Exact-commit CI passed for both changes: `46c71c45` (run 40619609849) and
`7a811855` (run 40620255967), including pinned ESP-IDF v6.1 firmware,
native host tests, simulated WebSockets, headless LVGL and React checks.
The TODO has 114 checked and 44 open entries. T05-01/04/05/11/18,
T10-01/02/03 and the physical acceptance gates remain **unchecked**:
no real station/DHCP handoff, OTA success/rollback, physical LCD/probe,
LittleFS power interruption or destructive reset was performed. Do not use
owner credentials, flash or reset an owner device for these tests. The
earlier hardware-dependent notes below remain historical evidence, not
permission to repeat device operations in a hardware-free run.


### Latest Ralph-loop checkpoint — 2026-10-27

#### Current stop point — 2026-10-27

The prior checkpoint `697d610` records the guarded Wi-Fi candidate-failure
rerun. This handoff commit adds an experimental firmware WebSocket
nonblocking-send path, an opt-in `WS_SLOW_BRIDGE` device probe, and
`CONFIG_LWIP_SO_RCVBUF` for a tiny-window test client. These changes build under
ESP-IDF v6.1 (image 0x1e63e0 bytes; 53% OTA-slot headroom). The full pinned
verifier passes: 9/9 host CTests, simulator slow-client/reconnect tests,
headless display, 56 web tests, formatting, lint, typecheck and production
build.

Firmware slow-client acceptance is **not proven**. A 30-second probe with a
1-byte requested receive buffer kept all 30 authenticated HTTP status checks
successful (maximum observed 256 ms), but accumulated only about 1.6 KB and
did not observe a send-failure/cutoff. A later 120-second run did not return an
inspectable bridge result. The board was reset afterward; the current flashed
image passed a 10-second bare-board smoke and returned to READY with a healthy
history mount, zero sensors, and backlight disabled. No LCD or probes are
connected. Start by reviewing the uncommitted WebSocket/test changes, then
design a deterministic cutoff test that separates TCP backpressure from HTTP
session eviction. Keep `T07-10` unchecked until that test passes.

The TODO remains 114 checked and 44 open. Important remaining gates include
physical board/LCD/probe/electrical validation (`T02`, `T04`), owner-network
association and phone onboarding (`T05-01/04/05/11/13/18`), LittleFS power-loss
acceptance (`T06-14/16`), destructive reset acceptance on a disposable target
(`T09-15`), successful OTA and rollback (`T10-01/02/03/09`), combined-load and
long-run hardware profiling (`T10-05/08/10`), and release reconciliation and
packaging (`T11-01/08/09`). Do not connect the LCD/backlight or probes until the
documented electrical checks pass. Existing-owner Wi-Fi preservation still
needs private owner credentials; do not issue a valid factory reset on this
owner device.

The reset UI/API now offers distinct confirmed actions to clear credentials and
device settings while preserving history, or to erase history too. The reset
choice is stored as an NVS intent and applied before history/network services
start. Contract generation, 7 focused web tests, all 56 web tests, the full
`tools/verify.sh`, and an ESP-IDF v6.1 build pass. `T09-15` remains open until
the reset is exercised against a disposable device/image; do not invoke a valid
reset on an owner device. The board reappeared as `/dev/ttyACM0`; the latest
firmware was flashed through the previously authorized workflow and passed the
bare-board smoke and non-destructive API suite. The API suite verifies four
concurrent authenticated WebSocket clients, fifth-client rejection, complete
snapshots, and HTTP responsiveness; a separate heartbeat measurement was
5,000 ms. No valid factory reset or peripheral connection was attempted.

The full pinned verifier passes: 9/9 host tests, simulator slow-client and
reconnect coverage, headless display, 56 web tests, lint, formatting, typecheck,
production build, and ESP-IDF v6.1 build (image 0x1e5ab0 bytes, 53% OTA slot
headroom). `T07-10` remains open only for firmware slow-client/backpressure
acceptance and remaining malformed-input integration coverage. TODO count remains
114 checked and 44 open.

The LittleFS format gate now has a host regression: formatting is allowed only
after mount failure when the entire partition reads erased (`0xff`); absent,
empty, or programmed data refuses formatting. T06-03 is checked. The full host
verifier and v6.1 build pass; image size is 0x1e4f90 bytes with 53% slot
headroom.

The TODO is now 114 checked and 44 open. Remaining gates require physical LCD,
probe, power-loss, OTA success/rollback, long-run and system-reset acceptance,
plus a private Wi-Fi credentials file for station/NTP/phone onboarding. See the
T02, T04, T05, T06, T09 and T10 entries in `TODO.md`; preserve the explicit
electrical safety gates before connecting peripherals.

The required backlog is [TODO.md](TODO.md); see the latest checkpoint below for
the current checked/open count. This is an active development build, not a
release. Firmware targets an ESP32-S3 with ESP-IDF v6.1; the web app is React,
TypeScript and Vite. Core acquisition, protected local HTTP, Wi-Fi setup,
settings, bounded temperature history, headless LVGL rendering, and an
`esp_lcd` ST7789/LVGL runtime are implemented. Physical LCD and sensor behavior
has not been validated. GPIO15 remains untouched; complete electrical checks
before attaching the backlight or probes.

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

### 2026-10-27 bare-board smoke update

The ESP32-S3 is currently accessible at `/dev/ttyACM0`. A 20-second
read-only smoke passed on the installed ESP-IDF v6.1 image: core/auth
self-tests, healthy LittleFS mount, READY with zero sensors and bus/LCD/backlight
disabled, about 8.57 MB free heap, four health samples at five-second cadence,
and no panic. No peripherals were attached and no flash was performed. See
[device smoke results](validation/device-smoke-2026-10-27.txt). This supersedes
the earlier statement that the board was unavailable, but sensor, LCD, Wi-Fi
association, and other hardware acceptance remain open.

The isolated five-fake-probe simulation image was used to complete the
two-ROM NVS restart test (`T03-10`), then the regular image was restored and
passed the bare-board smoke again. The simulation uses separate sensor
settings/history namespaces and did not enable physical GPIOs. See
[sensor NVS results](validation/device-sensor-nvs-results.txt).

The regular image also passed the current USB-loopback device API/security
suite and secret-leak scan (`T08-08`); the authenticated Settings network flow
is implemented and its focused UI tests pass (`T05-07`). The suite performed
and verified a safe restart, leaving the app in READY. No Wi-Fi credentials,
LCD, or probes were used.
Details are in [device API results](validation/device-api-results.txt); OTA
success/rollback, factory reset, station onboarding, and physical peripheral
checks remain open. The same run now verifies actual ESP32 WebSocket heartbeat
frames and closure after a client command (`T07-08/09`); device slow-client and
concurrent-client profiling remain open under `T07-10`. At that checkpoint,
the TODO count was 113 checked, 45 open. The reproducible host/device
acceptance scripts and evidence checklist are complete (`T11-07`); hardware feature gates remain
listed individually in `TODO.md`.

## Latest verification checkpoint — 2026-10-27

The ST7789 runtime is now integrated into the main sampling loop. It uses
documented SPI GPIOs (SCLK 12, MOSI 11, CS 10, DC 9, RST 14), 20 MHz SPI, and
two 240×40 PSRAM/DMA RGB565 LVGL buffers with byte swap and lock-protected UI
updates. GPIO15/backlight is not initialized. An opt-in
`CONFIG_MONITOR_LCD_TEST_PATTERN` shows RGB bars and corner markers for physical
orientation/color testing. Host display tests verify immediate live-reading
repaint, Wi-Fi state changes, and clock restoration. The full
`tools/verify.sh --firmware` passes: 9 host CTests, simulator checks, one
headless display test, 53 web tests, lint/format/typecheck/build and pinned
ESP-IDF v6.1 firmware build. Image is 0x1e3fd0 bytes with 53% OTA slot headroom.
No board was available: panel offsets/color order/rotation/readability/refresh,
DMA timing, and the test pattern remain unverified physically. At that LCD
checkpoint, TODO count was 102 checked and 56 open. No flash was performed.

Clock-jump follow-up: the aggregator now marks a post-step interval as
boot-relative if its aligned UTC bucket would collide with the preceding
trusted bucket, and resumes UTC once the next unused boundary is reached. Host
coverage includes the step/recovery path; timezone/DST and monotonic display
tests also pass. `T05-17` is checked. The firmware image is 0x1e3fd0 bytes.
TODO count is now 107 checked and 51 open. Host coverage now also exercises
cold-start unknown UTC, clock progression while offline, and same-boot history
transition from uptime timestamps to trusted UTC (`T05-14/15/16`). Device NTP
and Wi-Fi loss checks remain open under T05-18.

## Resume prerequisites

- The owner says the ESP32-S3 was unavailable at the 2026-10-26 checkpoint. As
  of 2026-10-27 it is visible at `/dev/ttyACM0`; verify availability on resume.
  Do not connect LCD/backlight or temperature probes until their
  electrical checks in T02-02/03/14 pass. Then run the ST7789 test-pattern
  configuration and measure panel orientation, color, refresh, and memory.
- Radio association, DHCP, NTP, and phone onboarding need an owner-created
  private JSON file with `ssid` and `password`; the owner has not supplied its
  absolute path. Do not put the file or credentials in Git or logs.
- T09-15 now exposes two explicit factory-reset choices: preserve history, or
  erase it. Both reset credentials and device settings. The implementation and
  host/device route checks still need verification; do not trigger a successful
  reset on the owner's board because that destroys stored credentials/data.

Earlier checkpoint:

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

Chromium 156/Linux measured the cold production auth page at 619 ms through
`load`, 186,348 compressed HTML/CSS/JS bytes transferred, about 590 kB decoded,
and 6.6 MB browser JavaScript heap. This local preview does not host auth/API,
so it returned 502 and did not measure the signed-in page, WebSocket, or ESP32
heap/socket load. See `RELIABILITY.md` and `T09-20`.

The host simulator's auth state now returns a schema-valid CSRF token; its live
integration checks auth and status HTTP responses in addition to WebSocket
behavior.

`python3 tools/check-secret-leaks.py` passed on the current private test setup:
no secret matches in 237 reviewable files/logs. Device-side OTA and oversized
body cases in the security API script still require the ESP32.

Host storage tests also exercise two concurrent 48-row CSV visits and verify
identical output. `tools/verify.sh --host` passes. T06-14 still needs device
HTTP concurrency plus physical LittleFS exhaustion, power-loss and reboot
acceptance.

T10-04 now documents the OTA recovery sequence: interrupted Wi-Fi/upload aborts
without selecting the partial slot; pending-verify boot can roll back; damaged
nonblank history is preserved and sampling continues. Recovery steps for AP,
USB flashing and raw history backup/restore are in `HISTORY_FORMAT.md`. Actual
power/network interruption and forced rollback remain hardware acceptance.

Latest pinned full verification passes: 9/9 host suites, simulator integration,
headless display, 53 web tests, lint, formatting, typecheck, production build,
and ESP-IDF v6.1 firmware build. The image is 0x17c8a0 bytes (63% OTA slot
headroom); the generated asset catalog contains 14 files, 639,363 raw / 203,464
gzip bytes. TODO currently has 99 checked and 59 open checkboxes. ESP32 was not
flashed because it is unavailable.

The USB device API script additionally rejects a JSON body over 2,048 bytes;
only Python syntax has been checked. Run it on the ESP32 to validate server
handling of oversized bodies and connection cleanup.

A guarded current-build candidate-failure rerun also passed: it first verified
that no station credentials were stored, tried a random nonexistent SSID, saw
the asynchronous 202/overlap 409 behavior and timeout, and confirmed setup AP
remained available with blank credentials. This is first-boot recovery evidence
only. Existing-owner-network preservation and successful station handoff still
need private Wi-Fi credentials; `T05-04/05/11/18` remain open.

## 2026-10-28 — Failed scan completion follow-up

Production `wifi_service` now checks `WIFI_EVENT_SCAN_DONE` driver status
instead of treating every timely completion as a successful scan. The
`WifiScanWindow` policy distinguishes failed versus timed-out completions;
both discard stale AP lists and release scanning state. Native regression
tests cover success, failure, duplicate/canceled events and deadline edges.
Formatting-only CI failed on `625263a8` (run 40769800800), then the corrected
exact source commit `bfb5ce45` passed run 40769907886 (native C++/Python,
simulated REST/WebSocket, headless LVGL, React, pinned ESP-IDF v6.1).
No ESP32, LCD, probes, owner network, OTA, reset or power interruption was
used. Keep T05-04/T05-18 and all physical acceptance gates unchecked.
