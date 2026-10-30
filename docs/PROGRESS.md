# 2026-10-30 — Connected-board acceptance updates

A freshly built 2,001,568-byte firmware image was uploaded to the ESP32-S3 using
an authenticated station HTTP session. The device accepted the image, booted
from `ota_1`, reconnected using saved settings, passed the startup health gate
(`ota_state=valid`), and retained administrator access. Authenticated API
status and the embedded HTML root/hashed JavaScript asset returned HTTP 200
through the USB bridge. A 35-second post-update smoke passed with healthy
history, zero sensors, five-second health cadence, and no panic. T10-01 and
T10-03 are complete. A second authenticated OTA returned the board to
`ota_0`, also valid, with admin access and saved Wi-Fi intact. A pre-confirmation
reset of the next pending image triggered bootloader rollback to `ota_0`; the
prior event-journal boundary remained intact. T10-01 and T10-03 are complete and
A→B→A/early-reset rollback passed. Failed-service rollback, LCD status, and
populated temperature-history comparison remain open. See
[OTA validation](validation/device-ota-results.txt).

During a live 60-second poll after the monitor was ready, the provisioning AP
stayed closed. The five-second BOOT behavior remains unverified; the live status
poll does not establish button timing or GPIO routing. The board photo remains pending. LCD and probes remain disconnected.

The numbered tracker is now **123 complete, 22 open of 145**.

## 2026-10-30 — Bare-board and WebSocket backpressure acceptance

The current normal ESP-IDF v6.1 image passed a 20-second bare-board smoke at
`/dev/ttyACM0`: 16 MiB flash, 8 MiB PSRAM, healthy LittleFS/event journal,
READY, zero sensors, disabled 1-Wire/backlight, five-second health cadence,
and no panic. The complete authenticated USB-loopback API suite passed.

T07-10's remaining device gap is closed. A 120-second tiny-window stalled
WebSocket was cut off by firmware backpressure handling. All 120 authenticated
HTTP status checks completed, with maximum latency 255 ms; health logs remained
on five-second cadence. Four simultaneous sockets receive valid snapshots and
the fifth is rejected with 503. The slow-client harness now retains health
records consumed during the bridge operation, so cadence validation includes
the full interval. See `validation/device-api-results.txt` and
`validation/device-smoke-2026-10-30.txt`. The first 30-second probe was too
short to trigger cutoff; no cutoff claim is based on that run.

The post-fix full `source /home/phil/esp/esp-idf-v6.1/export.sh &&
./tools/verify.sh --firmware` passed: 19/19 host CTests, 14 Python tests,
simulator/WebSocket checks, headless LVGL, 56 web tests, formatting, lint,
typecheck, production build, SDK pin verification and ESP-IDF v6.1 compile.
Firmware image is `0x1e8aa0` bytes with 52% of the smallest OTA slot free.

The board remains on the normal firmware with no LCD or probes attached. This
does not verify storage power interruption, OTA rollback, or sustained load.

## 2026-10-30 — Wi-Fi handoff and saved-network recovery

Using the owner-authorized open network, blank-state setup saved
credentials only after DHCP success, closed the provisioning AP, and reached
NTP synchronization. After a USB-triggered normal reboot, the board reconnected
from NVS. A subsequently submitted nonexistent candidate returned 202 (overlap
409), timed out, and restored the prior station with saved credentials and
DHCP while keeping the setup AP available. This closes T05-01/04/05/11. Testing
used the USB API/setup bridge, not a phone. No password was needed or recorded;
the SSID is not included in GitHub-visible test output.

## 2026-10-29 — T11-08 release notes draft (no release approval)

The canonical T11-08 requests release notes and known issues following
release-blocking verification. A dedicated draft did not exist, so
[RELEASE_NOTES_DRAFT.md](RELEASE_NOTES_DRAFT.md) now records existing
software features, evidence limits, physical acceptance blockers,
known network/data/electrical limitations, and exact-SHA provenance
and owner signoff placeholders. The draft does not claim physical
testing, production approval or a final version.

The refreshed baseline exact master `5189e9b9d254ee1795d189f8c98665560c51fa15`
passed GitHub Actions (native C++/Python, simulated API/WebSocket,
headless LVGL, React and pinned ESP-IDF v6.1 checks). This is a
documentation-only improvement under T11-08; final exact-SHA CI for
the documentation commit must be checked separately.

Hardware-only acceptance is still outstanding for board/LCD/backlight, five DS18B20 probes, owner Wi-Fi/phone/BOOT, controlled LittleFS interruption and measured retention, real WebSocket backpressure, successful OTA/rollback, disposable-device factory reset, and combined 24–72-hour heap/watchdog soak.
The numbered checklist stays **114/145 complete, 31 open**. No
hardware, credentials, flashing, OTA, destructive reset or power cut
was used.

## 2026-10-29 — Include initial OTA selector in CI firmware artifact (software-only)

The release provenance checker correctly listed and hashed
`firmware/build/ota_data_initial.bin` in `flasher_args.json` and
`SHA256SUMS.txt`, but the GitHub Actions upload configuration omitted the
binary. A downloaded archive could therefore lack a required flash image
and fail checksum verification despite the in-run provenance checks.

The upload now explicitly includes `ota_data_initial.bin`. A Python
contract regression compares every source/flash input in the synthetic
provenance checksum list (plus manifest/checksums and recursive web
assets) with the CI artifact upload path list, guarding against future
omissions. Initial `9f8e5a84` failed due to an incorrectly escaped
newline in the new test; corrected source
`b9e6b763bfc1dce6c9063dcad7bf79b11cf8e9e9` **passed** exact-SHA
GitHub Actions `40993467640`: native C++/Python, simulated REST/
WebSocket, headless LVGL, React, pinned ESP-IDF v6.1 build and
successful artifact upload. Uploaded artifact ID `12950726328`,
`esp32-s3-firmware-b9e6b763bfc1dce6c9063dcad7bf79b11cf8e9e9`.
The remote archive was not independently downloaded and extracted;
success establishes upload and source-contract coverage, not release
approval or on-device flash behavior.

**Numbered TODO: 114/145 complete (31 open).** T11-08 remains unchecked.
Still blocked on actual board/LCD/probe electrical and runtime tests,
owner-network onboarding, physical LittleFS power interruption,
OTA A/B/rollback, isolated disposable-device factory reset, sustained
heap/latency/soak, and final release notes/signoff. No hardware,
credentials, flashing or destructive operations were used.

## 2026-10-29 — Reject extra flash images and unexpected partition flags (software-only)

After binary partition-table cross-checking, the release provenance gate
still allowed additional flash images in unused address gaps, and the CSV
reader ignored unexpected flags or extra columns. The packaging validator
now permits **only** the four pinned flash images (bootloader, partition
table, OTA selector and application) and requires exactly six CSV columns
with empty flags, consistent with the validated unencrypted layout.

Two additional synthetic Python regression methods cover a fifth image
in the otherwise unallocated flash gap and CSV flags/extra-column drift
(twelve provenance test methods total). Exact source
`9f269a6811f5c9e6780107c35d6ba6ab5198af59` **passed** GitHub
Actions `40991655488`: native C++/Python, simulated REST/WebSocket,
headless LVGL, React, pinned ESP-IDF v6.1 firmware compilation, and
CI-only artifact provenance generation/upload. This is not on-device
flash, OTA, partition persistence, or release qualification.

**114/145 numbered tasks complete (31 open).** T11-08 stays open;
board/LCD/probes, owner-network onboarding, real LittleFS power loss,
OTA slot transitions/rollback, disposable-device factory reset,
24–72h soak and final release notes/signoff remain prerequisites.
No hardware, credentials or destructive operations were used.

## 2026-10-29 — Verify the flashed partition table against source CSV (software-only)

The CI release-provenance generator previously checked every partition in
`firmware/partitions.csv` but only hashed the generated binary. A stale
`partition_table/partition-table.bin` could therefore disagree with the
validated CSV while still producing a self-consistent manifest. The
generator now decodes ESP-IDF 32-byte partition records, checks their
MD5 marker and digest, padding, labels and flags, and requires the
complete binary type/subtype/offset/size map to match the CSV exactly.

Two synthetic Python regression methods cover a stale binary with
recomputed valid MD5, CSV drift with unchanged binary, bad/missing MD5,
non-padding bytes and unexpected flags. The corrected exact source
`656fdc0f2c887326b5f468eddd022410e761572f` **passed**
GitHub Actions run `40990485662`: formatting, native C++/Python,
simulated REST/WebSocket, headless LVGL, React, pinned ESP-IDF v6.1
firmware build and CI-only provenance generation/artifact upload.
The initial `673fffd8` source had an escaped-byte literal error;
`656fdc0f` corrected it before the successful run. This is artifact
integrity validation, not on-device flash/OTA success. T11-08 stays open.

Numbered TODO remains **114/145 complete, 31 open**. Physical
board/pins/backlight, five DS18B20 probes, real LCD/LVGL, owner-network
provisioning, LittleFS power interruption, actual OTA/rollback,
disposable-device factory reset and 24–72h load/soak evidence are still
required. No device, Wi-Fi/admin secret or destructive operation was
used.

## 2026-10-29 — Full 16 MiB flash layout and OTA selector release-provenance checks (software-only)

Expanded `tools/release_provenance.py` to validate **every** ESP-IDF
partition rather than just the two OTA slots: mandatory NVS/otadata/PHY,
two OTA application slots and history; unique names and expected types;
app/data alignment; strictly positive extents; non-overlap; and bounds
within the 16 MiB ESP32-S3-N16R8 flash. All flasher images must fit
physical flash and must not overlap other partitions. The one intentional
exception is ESP-IDF's `ota_data_initial.bin` at the exact `otadata`
offset, with its size bounded by that partition. The manifest now
records `flash_size_bytes` and the validated `partition_layout`.

Updated the synthetic fixture to match the real four-image flasher map
and added two tests covering malformed/duplicate/misaligned/overlapping/
out-of-flash partitions, unauthorized extra images and invalid OTA
metadata placement/size (eight provenance tests total). The first
`170b4a66` workflow was cancelled by a newer push. `42635006`
passed firmware compilation, 19 native CTests, Python provenance
regressions, simulated REST/WebSocket, headless LVGL and React checks,
but failed the final provenance stage because it rejected ESP-IDF's
legitimate initial OTA selector image. Corrected exact source
`d89b505c0eb5b47e4e193fb097ff368ee92367ed` **passed**
GitHub Actions `40985438288`, including the pinned ESP-IDF v6.1
build, full host/display/web verification, provenance generation and
artifact upload. No owner hardware or secrets were used.

Numbered TODO remains **114/145 complete (31 open)**. T11-08 is
still only partially prepared: generated CI firmware is explicitly
`CI_BUILD_ONLY_NOT_RELEASE_APPROVED`. Board/USB and LCD electrical
checks, five-probe 1-Wire reliability, owner Wi-Fi/phone onboarding,
actual LittleFS power-loss, successful OTA/rollback, isolated destructive
reset, on-device memory/latency and 24–72h soak, plus final release
notes/signoff, remain unverified.

## 2026-10-29 — CI-only build provenance and OTA image safety gate (software-only)

Added `tools/release_provenance.py` to generate a deterministic, source-SHA-bound
`firmware/build/provenance/manifest.json` and `SHA256SUMS.txt` for every
passing GitHub Actions firmware build. It validates the actual ESP-IDF
`flasher_args.json` map, required bootloader/partition/app offsets, binary
non-overlap, both equal-sized OTA slots, application fit/headroom, matching
firmware/web/npm-lock versions, safe regular paths (no traversal/symlinks),
and SHA-256 for each flash image and built React asset. The manifest embeds
the pinned ESP-IDF revision and is **explicitly marked**
`CI_BUILD_ONLY_NOT_RELEASE_APPROVED`. The CI artifact now includes the
flashable development images, web assets, source lockfile/partition inputs,
manifest and checksum list. A new six-case synthetic Python test suite
exercises deterministic output and refusal of unsafe/missing/oversized or
version-drifted inputs; `tools/verify.sh` runs it before the SDK build.

Exact source commit `10c9e480318c8dc66713aa6be6109352fcbc023c`
**passed** Actions run `40978033002`: C++ formatting, native C++/Python
(including six provenance tests), simulated REST/WebSocket, headless LVGL,
React lint/tests/build, pinned ESP-IDF v6.1 compilation, provenance
generation and successful artifact upload. The uploaded artifact was
`esp32-s3-firmware-10c9e480318c8dc66713aa6be6109352fcbc023c`
(artifact ID `12943841915`); its reported expiration was
2026-11-02 UTC. See [RELEASE_PROVENANCE.md](RELEASE_PROVENANCE.md) for
verification and safety restrictions.

Numbered TODO remains **114/145 complete (31 open)**. T11-08 is partially
prepared but cannot be checked until release-blocking hardware gates,
signed-off known issues/release notes and final qualification are complete.
Sensor/LCD, owner Wi-Fi/phone onboarding, real LittleFS interruption,
successful OTA/rollback, destructive reset on an isolated device, and
sustained on-board heap/Wi-Fi/flash testing remain unverified. No ESP32,
owner credentials, flashing, power cut, or destructive reset was used.

## 2026-10-29 — Validate compacted event-journal image before rename (software-only)

EventStore::compact previously fsynced a new temporary journal and verified
the original journal before rename, but did not validate the actual temporary
bytes. A same-size, CRC-valid in-place modification during temporary-file
fsync could replace the only good durable journal with an altered image.
The writer now checks the temporary path is a regular file, its exact size,
and its complete expected FNV-1a fingerprint **before** atomic rename.
A mismatch fails compaction, removes the temporary file, preserves the
original journal, and latches appends closed until successful recovery.

Extended the existing native Linux `--wrap=fsync` regression to modify
the compacted temporary record after fsync with a valid recalculated CRC
and unchanged size. The test verifies refusal, byte-for-byte preservation
of the original two-record journal, cleanup of the temporary path, read
quarantine, successful explicit recovery, and normal subsequent compaction
with monotonic sequence IDs. Exact implementation commit
`27f141c84860044faf361f2e6a6f6c32e2277adf` **passed** GitHub
Actions run `40971837074`: C++ formatting, native C++/Python,
simulated REST/WebSocket, headless LVGL, React checks, and pinned
ESP-IDF v6.1 firmware compilation. FNV-1a detects accidental changes
rather than malicious forgery; on-device LittleFS latency and actual
power-loss durability remain unverified.

Numbered TODO remains **114/145 complete (78.62%)**, 31 open.
T06-14/T06-16/T10-05 and physical sensors/LCD, owner-network
onboarding, successful OTA/rollback, disposable-device reset and
24–72-hour soak acceptance stay unchecked. No hardware, credentials,
flashing, OTA or destructive operations were used.

## 2026-10-29 — Verify event-journal image after fsync (software-only)

EventStore::append previously validated the full recovered journal before
writing but acknowledged a new sequence after checking only the final file
size and POSIX inode. A CRC-valid, same-size in-place rewrite during the
write/fsync interval could therefore make an append appear successful even
though its durable journal image differed from the expected contents.
The writer now verifies the complete expected FNV-1a fingerprint, final
event bytes, and file size after fsync/close and before advancing its
in-memory cursor. A mismatch quarantines reads and writes until a
successful explicit recovery. FNV-1a detects accidental changes, not
adversarial forgery; concurrent writers are still unsupported.

Added `firmware/tests/event_append_integrity_tests.cpp` with deterministic
GNU-ld `--wrap=fsync` host-only fault injection. The test modifies either
the first or newest record *after* the production fsync, retaining a valid
CRC and identical file size, and verifies append refusal, unavailable
pages, recovery latch, repair plus recovery, and sequence continuity.
The first implementation `a607f3af` was followed by a test correction;
`ccd280f0` failed C++ formatting-only CI (Actions `40964337076`).
Corrected source `174c6d639f9b3d211c59327d27cb1c2f4a05feb4`
**passed exact-SHA GitHub Actions `40964463330`**: C++ formatting,
native C++/Python, simulated REST/WebSocket, headless LVGL, React checks,
and pinned ESP-IDF v6.1 firmware compilation. This is host fault
injection, not an actual LittleFS power cut or ESP32 concurrency/flash
latency measurement. Additional post-write journal reads must be
profiled on hardware.

Numbered TODO remains **114/145 complete (78.62%)**, 31 open.
T06-14/T06-16/T10-05 and all physical probes/LCD, owner-network
onboarding, OTA success/rollback, disposable reset, and sustained-load
acceptance gates remain unchecked. No owner secrets, hardware access,
flashing, OTA or destructive operations occurred.

## 2026-10-29 — Preserve altered completed history before eviction (software-only)

HistoryStore previously checked the recovered active segment's size, identity
and fingerprint but did not retain a seal for older completed segments. A
same-inode, same-size, CRC-valid modification to an older segment could
therefore be silently discarded by the next retention eviction. Recovery
now seals every segment (number, size, FNV-1a fingerprint and POSIX identity).
Append checks the retained segment set and metadata, preflights each
would-be eviction victim before writing, and rechecks the seal before
deletion. Successful append/rollover updates seals; recovery rebuilds them.
Failed validation latches the writer closed until explicit recovery.

Native C++ regression writes four segments, alters the oldest record with
a valid recalculated CRC while preserving its size/inode, verifies the
fifth append fails without creating a new segment or erasing evidence,
and verifies that repair alone does not bypass the recovery latch.
After explicit recovery the fifth append succeeds, the oldest segment
is evicted normally, and all four retained records export successfully.

Source commit `ff2941d0187e7be8a0b40c990fb6d75bc52e81fa` **passed**
exact-SHA Actions run `40956362940` (C++ formatting, native C++/Python,
simulated REST/WebSocket, headless LVGL, React, and pinned ESP-IDF v6.1
firmware compilation). FNV-1a is accidental-change detection rather
than cryptographic authentication. Per-segment RAM and extra recovery
and eviction reads need ESP32 LittleFS/heap/latency measurements. The
numbered TODO remains **114/145 complete** (31 open); T06-14,
T06-16, T10-05, and all physical probe/LCD, owner Wi-Fi, OTA success
and rollback, disposable reset, and sustained-load gates remain
unchecked. No physical hardware, secrets, flashing, OTA or destructive
operations were used.

## 2026-10-29 — Validate all retained history after early visitor stop (software-only)

HistoryStore::visit previously returned success as soon as the callback
returned false, allowing a short page to hide a malformed later record
in the same segment or a subsequent segment. It now stops invoking the
callback but continues bounded decoding, CRC/length and file identity
checks, and full-segment/cross-segment fingerprint verification before
reporting success. Native regression verifies corrupt suffix refusal in
both segment layouts, exactly one delivered row, and success after
restoring the original record. This preserves callback early-stop
semantics but adds read I/O to short history queries.

Initial implementation `cf3cd5717f6114f6034234c3aa9cea7a858cce61`
failed C++ formatting (Actions `40946137220`). Corrected exact source
`8182737ef1b47ae802bc571a566441280e26ec4d` **passed** Actions
`40946233196`, including C++ formatting, native C++/Python tests,
simulated REST/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1
compilation. This is host/simulator evidence, not actual LittleFS
power-loss or measured ESP32 performance. TODO remains **114/145
numbered tasks complete (78.62%)**, 31 open. Keep T06-14/T06-16/T10-05
and all sensor/LCD, owner-network onboarding, OTA success/rollback,
disposable reset and 24–72-hour soak acceptance gates open. No physical
hardware, owner credentials or destructive operations were used.

## 2026-10-29 — Verify active history integrity before and after append (software-only)

HistoryStore previously required a recovered active segment to retain its
path, POSIX inode and byte count, but an external same-size, CRC-valid
in-place rewrite could survive these checks. The next append (including
rollover/eviction) could then extend history whose validated contents had
changed. Recovery now retains a bounded 64-bit FNV-1a fingerprint of the
active segment; append rechecks those contents before writing or rolling
over, and compares the expected full segment fingerprint after fsync before
reporting success. Any mismatch latches the writer closed pending recovery.
This is accidental-change detection, not cryptographic authentication.

Native C++ regression covers both same-segment append and rollover,
CRC-valid same-size/inode replacement, refusal without new segments or
modified byte counts, refusal after repair until explicit recovery, and
successful recovery/append with two intact rows. The first implementation
commit `5038536db8ba562232375e386a7bc8cee6968b4f` failed only the C++
formatting gate (Actions `40941859621`). The corrected exact SHA
`791451eea2946ed747aa50a56e52eeac998314ea` **passed** GitHub
Actions `40942128655`: C++ formatting, native C++/Python, simulated
REST/WebSocket, headless LVGL, React checks and pinned ESP-IDF v6.1
firmware compilation. Added segment reads need ESP32 LittleFS/heap/timing
profiling; none was measured here. TODO stays **114/145 complete** (31
open), with T06-14/T06-16/T10-05 and all physical sensor/LCD,
owner-network/phone/DHCP, OTA/rollback, disposable reset and soak gates
unchecked. No hardware, credentials, flashing, OTA, power interruption
or destructive operation was used.

## 2026-10-29 — Cross-segment history export integrity (software-only)

HistoryStore::visit previously fingerprinted each segment before and after its
own traversal. A callback processing a later segment could still rewrite an
earlier, already-checked segment in place with a valid recomputed CRC and
unchanged inode/length, allowing a mixed-time export to report success. The
export now retains bounded per-segment fingerprints and (on POSIX) file
identities, and rechecks all visited segments at completion or early visitor
termination. Native regression exercises both paths with two segments,
mutates the earlier record during the second callback, and confirms normal
export after restoring the original record. FNV-1a detects accidental
modification, not cryptographic tampering. The additional streaming reads
require physical LittleFS/heap/watchdog profiling.

Initial `88ba3f69` failed clang-format (Actions `40937768233`);
formatted `f2b15226` failed native -Werror on uninitialized snapshot fields
(Actions `40937869255`). Corrected exact-source
`f9c379883ab20d2eac67f498b3d3638e60b2b5b6` **passed** GitHub
Actions `40938539344`: native C++/Python, simulated REST/WebSocket,
headless LVGL, React and pinned ESP-IDF v6.1 compilation. Numbered TODO
remains **114/145 (78.62%)**, 31 open. T06-14/T06-16/T10-05 and all
physical sensor/LCD, owner-network, OTA/rollback, disposable factory-reset
and soak acceptance gates remain unchecked. No hardware, credentials,
flashing or destructive operations were used.

## 2026-10-29 — Reject CRC-valid in-place history changes during export (software-only)

HistoryStore::visit previously verified segment path/inode, size and each
record's CRC, but a callback could rewrite an earlier record in place with a
valid recomputed CRC and unchanged inode/length. The export could then report
success despite having visited a mixed-time snapshot. Each segment is now
fingerprinted with a bounded streaming FNV-1a pass before traversal and
rechecked after traversal, including early visitor termination. Native
regression rewrites the first of two records in place during the first
callback, confirms the export refuses success, restores the original bytes,
and confirms two-record export success. The fingerprint detects accidental
changes, not malicious forgery; the additional segment reads require device
performance/heap/watchdog measurement under T10-05.

Initial source `3aa914e0` failed clang-format (Actions 40927808243);
formatted `011b6a1e` failed native compilation on a duplicate fixture variable
(Actions 40927955597); `9513e6f0` failed formatting on the renamed assertion
(Actions 40928607784). Corrected exact source
`1ea6773c47c2dcb9ff874cd071c0b19f93c5651b` **passed** GitHub Actions
`40928698651`: native C++/Python host tests, simulated REST/WebSocket,
headless LVGL, React checks, formatting and pinned ESP-IDF v6.1 compilation.
Numbered TODO remains **114/145 (78.62%)** with 31 open. T06-14/T06-16
still require actual LittleFS exhaustion, concurrent device export,
reboot/power-loss verification. Physical sensors/LCD, owner-network Wi-Fi,
OTA success/rollback, disposable reset and sustained-load acceptance also
remain open. No hardware, credentials, flashing or destructive operations
were used.

## 2026-10-29 — Preserve journal on exhausted 64-bit event sequence (software-only)

The durable journal uses monotonically increasing 64-bit API cursors. An
append after sequence UINT64_MAX previously triggered compaction **before**
checking for sequence overflow, discarding older events even though the new
record could not be encoded. The guard now runs after duplicate suppression
but before compaction; new distinct events fail without modifying the journal,
and existing pages remain available. A native regression preloads two valid
records with sequences UINT64_MAX-1 and UINT64_MAX at the capacity boundary,
then verifies append refusal, unchanged file length and retained pagination.
Initial `7dcf17f9` failed C++ formatting (run 40919078045); formatted
`03f821a5` exposed the pre-existing placement of compaction before the new
guard (run 40919196805). Corrected source
`14259ea170fc58595854c382869551919f22d8e4` **passed** exact-SHA
GitHub Actions **40919934549**: native C++/Python, simulated REST/WebSocket,
headless LVGL, React and pinned ESP-IDF v6.1 compilation. Numbered TODO
remains **114/145 (78.62%)**, with 31 open tasks. T06-14/T06-16 still need
physical LittleFS exhaustion, power interruption and reboot; owner-network,
LCD/probes, OTA/rollback, disposable reset and sustained hardware-load
acceptance also remain open. No hardware, secrets, flashing or destructive
operations were used.

## 2026-10-29 — Bounded streaming event journal recovery (software-only)

EventStore::recover() no longer materializes an arbitrarily large journal in
RAM before checking its configured capacity. Recovery preflights the file size,
streams and CRC-validates fixed 60-byte records, checks monotonic sequences,
detects concurrent growth, verifies the original image (including a valid
interrupted-header tail) before any truncation, and revalidates the resulting
durable prefix. A native regression uses a preserved 32 MiB sparse oversized
journal; the existing torn-tail, CRC, compaction and pagination tests remain
in the verification suite. Initial exact-head `1bc9a171` failed formatting-only Actions `40910018910`;
`854a2caca` passed formatting and host tests but failed pinned ESP-IDF
compilation on a stale variable name (Actions `40910434198`);
corrected exact-source `3c65f8d20699e1a009ec3a2a67ff67633b27d06a` **passed** Actions `40911260280` (native C++/Python, simulated REST/WebSocket, headless LVGL, React, formatting and pinned ESP-IDF v6.1 compilation). No physical LittleFS
power-loss, reboot, sensor/LCD, owner-network, OTA, reset or soak acceptance
is claimed. T06-14/T06-16 remain open; numbered TODO stays 114/145.

## 2026-10-29 — Full-stream event journal fingerprint integrity (software-only)

EventStore previously checked the durable final record, per-record CRCs and
monotonic sequence ordering, but a CRC-valid same-size edit to an earlier
record could still pass pagination and be carried into compaction. The store
now maintains an incremental FNV-1a 64-bit fingerprint across the entire
recovered journal, verifies it before append/compaction, and checks it during
the complete API pagination scan. Recovery and compaction rebuild the
fingerprint; successful appends extend it. Native C++ regression changes
the first of three events while preserving the inode, size, sequence order,
last record and valid recomputed CRC, verifies both short/full pages and
append reject it, and verifies restored data after repair, recovery and
compaction. This is accidental-corruption detection, not cryptographic
authentication; full-stream verification increases append read I/O and
requires device-load timing under T10-05.

Implementation `6d7c4954` failed formatting-only Actions 40900779253;
corrected exact SHA `dc50bf1731e40a29e8fcd975a976567fbb2ae618`
**passed** Actions 40901012137: native C++/Python, simulated REST/WebSocket,
headless LVGL, React, formatting and pinned ESP-IDF v6.1 compilation.
Numbered checklist remains 114/145 complete (31 open). T06-14/T06-16
and physical sensor/LCD, owner Wi-Fi, OTA success/rollback, disposable
reset, LittleFS power interruption and sustained-load gates remain open.
No hardware, owner credentials, flashing, OTA, reset or power loss was used.

## 2026-10-29 — Reject CRC-valid final journal mutation in API pagination (software-only)

EventStore::page() now compares the durable final record with the last
recovered/appended event before and after scanning the journal. Previously,
a same-inode, same-size modification with a recomputed valid CRC and a
monotonic sequence could be served as a healthy API page, even though append()
would reject the changed record. Native storage regression writes a
different event kind with a valid CRC into the last of two records, verifies
both short and full pages fail closed, restores the original record, and
checks normal pagination resumes. This does not detect arbitrary mutations
of earlier records with valid CRCs; those remain subject to CRC/sequence
validation. Host-only testing is not evidence of physical LittleFS,
power-loss, owner-network, OTA or device acceptance. Exact-source commit `a29c18bd1c1338844d15ac29452c32de85a0ae4c` passed GitHub Actions run `40895270734`, including native C++/Python, simulated REST/WebSocket, headless LVGL, React, formatting and pinned ESP-IDF v6.1 compilation.

T06-14/T06-16 remain open, as do physical sensor/LCD, owner-network,
OTA/rollback, disposable reset and sustained-load acceptance. No hardware,
credentials, flashing, factory reset or power interruption was used.

## 2026-10-29 — Reject same-size final event-record corruption before append (software-only)

EventStore append now verifies that the durable last 60-byte record still
matches the recovered/appended event before duplicate suppression, compaction,
or a new write. This closes a same-inode/same-size mutation gap in which a
duplicate could previously be acknowledged without validating its durable
record, or a new event could be appended after a corrupted final record.
Native regression mutates the sequence and event kind in place, checks both
duplicate and distinct append refusal, quarantined API reads, and restored
sequence continuity after repair and fresh recovery. This checks only the
final record, not arbitrary earlier same-size corruption; API pagination
continues to scan all records. Initial source `0bbb5262` failed
C++ formatting-only CI run `40890814193`; corrected source
`c9699dada0e2d95e1a98fdd38c8708a3ffc86441` **passed** exact-SHA
GitHub Actions run `40890960369`, including native C++/Python, simulated
REST/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1 compilation.

T06-14/T06-16 remain open for physical LittleFS/power interruption and
reboot. No ESP32, LCD, probes, owner Wi-Fi, OTA, factory reset, or flashing
was used; no credentials were accessed.

## 2026-10-29 — Fail closed on unverified event journal API pages (software-only)

`EventStore::page()` now requires a successful recovery and quarantines reads after
an append/recovery failure. Previously a valid original journal restored after a
failed append could be served as healthy without fresh verification; an instance
never recovered could also report an empty file as available. Native regression
checks the pre-recovery state, a failed append followed by file restoration,
recovery, and normal pagination. Exact-source `97bbecda1e9cf2c11c07b29e16e9985ed6528a06`
**passed** GitHub Actions `40879054815`: C++ formatting, native C++/Python,
simulated REST/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1.

Numbered TODO remains **114/145 complete (31 open)**. T06-14/T06-16 still
require physical LittleFS failure/power interruption and reboot acceptance.
Physical sensors/LCD, owner-network AP+STA onboarding, OTA success/rollback,
disposable-device reset and 24–72-hour soak remain unverified. No devices,
owner credentials, flashing or destructive actions were used.

## 2026-10-29 — Reconcile storage CI and protect history export file identity (software-only)

The concurrent storage fixture introduced two local variables named `size_guard`
(one HistoryStore, one EventStore), causing exact-head `90037ce6` to fail
native compilation (Actions `40873611562`). Renaming the event fixture in
`b53bb113` restored passing exact-SHA CI (run `40874634016`).

HistoryStore::visit now records each history segment's POSIX device/inode
before export and rechecks it after every callback, on early callback stop
and after each segment. A valid ordinary file copied to the same pathname
with identical byte length cannot silently replace the segment while an
export is in progress. The regression replaces the segment after its first
record, expects export refusal, restores the original and confirms both
records can then be exported. ESP-IDF LittleFS has no reliable inode
identity; its path/type checks remain, and this host-only check is not a
claim of physical flash integrity.

Initial `9b67d130` failed clang-format (Actions `40875626820`).
Formatted `8d252196` failed native `storage_recovery` because the test
configured less than twice its segment size (Actions `40876024040`).
Corrected `a0b22cbaa7256175c53a9f059418680d975fe9c4` **passed** exact-SHA
GitHub Actions `40876690573`, including all native CTests, Python checks,
simulated REST/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1 build.

The numbered TODO remains **114/145 complete (31 open)**. T06-14/T06-16
stay unchecked for physical LittleFS/power interruption, device concurrency
and reboot acceptance. Physical sensor/LCD/electrical checks, owner-network
provisioning, OTA success/rollback, destructive factory reset on disposable
hardware, sustained heap/Wi-Fi soak and release gates remain open. No owner
device, Wi-Fi/admin credentials, flashing, OTA, reset or power interruption
was used in this cycle.

## 2026-10-29 — Detect same-inode event journal size changes (software-only)

EventStore now records the durable journal byte count after recovery, append
and compaction. Before duplicate suppression, writes or API pagination it
rejects an in-place truncate or external append even when the original path
and inode are unchanged; a mismatch latches the writer until re-recovery.
This prevents a stale duplicate from falsely succeeding after durable events
were removed and prevents appending an out-of-sync sequence to an altered file.
Native storage regression exercises both size changes, unavailable API pages,
writer latching and sequence continuity after a fresh recovery. It does not
claim to detect arbitrary same-size in-place corruption without a read.

Exact-source commit `1ff9f96ea84a67f8b00fe5a4a5381e5f196720fa` passed
GitHub Actions run `40871643869`: native C++/Python, simulated REST/WebSocket,
headless LVGL, React checks and pinned ESP-IDF v6.1 firmware compilation.
T06-14/T06-16 remain open for physical LittleFS exhaustion, power interruption
and reboot. ESP32/LCD/probes, owner Wi-Fi, OTA/rollback, destructive reset and
24–72-hour soak acceptance were not performed. No owner credentials or devices
were accessed. Numbered TODO remains 114/145 complete (31 open); final
acceptance and optional backlog are separate.

## 2026-10-29 — README capability and safety reconciliation (documentation-only)

Updated the repository overview to match implemented React/LVGL/firmware
capabilities and to distinguish software verification from physical panel,
DS18B20, owner Wi-Fi, OTA, LittleFS power-loss and factory-reset acceptance.
GPIO15 backlight remains a documented electrical hold point. No physical
checks or hardware actions were performed. T11-01 remains open until the
measured final board/LCD pinout and behavior can be reconciled.

## 2026-10-29 — Bind native event-journal operations to recovered file identity (software-only)

Native EventStore now records the journal device/inode on successful recovery,
refuses a substituted ordinary file before appending, compaction and API
pagination, verifies the opened append descriptor, and checks identity after
writing. Successful compaction rebinds the identity to the newly renamed
journal. A native regression substitutes a valid copy at the same path,
checks that API reads and writes refuse it without changing bytes, then
restores the original, recovers and appends with correct sequence continuity.
ESP-IDF LittleFS retains path/type checks because its VFS does not expose
reliable inode identity. Exact-source `d5bb8f32` passed GitHub Actions run `40865064578` with
native C++/Python storage regressions, simulated REST/WebSocket, headless LVGL,
React and pinned ESP-IDF v6.1 firmware compilation. No physical flash, power
interruption, owner Wi-Fi, LCD/probes, OTA, factory reset or soak test occurred. T06-14/T06-16 remain unchecked.

## 2026-10-29 — Bind native history append to recovered active segment identity (software-only)

Native HistoryStore now retains the active segment device/inode after successful
recovery, refuses an ordinary same-name replacement before append, checks the
opened descriptor against the recovered identity, and verifies the pathname
still resolves to the written inode after flush/fsync/close. Native storage
regression substitutes a valid regular file, confirms its bytes are unchanged
and the writer remains latched, then restores the original, recovers and
appends successfully. This extends the existing directory identity and symlink
safeguards; ESP-IDF LittleFS retains path/type checks because its VFS has no
reliable inode identity. The regression and pinned ESP-IDF v6.1 build require
exact-commit GitHub Actions verification. Initial `964e7ba4` failed
formatting-only CI run `40863633750`; the source expression was simplified. Corrected source `da434a48`
passed formatting and ESP-IDF v6.1 compilation but storage_recovery failed
because the new test expected three rows after restoring a one-row baseline;
the correct post-append count is two. Corrected exact-SHA `7582e64b` passed GitHub Actions run `40864207105`
(native C++/Python, simulated REST/WebSocket, headless LVGL, React and pinned
ESP-IDF v6.1). This verifies host-only active-segment substitution refusal,
not physical LittleFS or power-loss acceptance. T06-14/T06-16, physical probes/LCD, owner-network onboarding, OTA,
reset, sustained load and power-loss acceptance remain unchecked. No hardware,
credentials or destructive operations were used.

## 2026-10-29 — Bind history writers to recovered directory identity (software-only)

Native HistoryStore writers previously rejected missing and symlinked history
directories but could append into a different ordinary directory substituted
after recovery. Successful host recovery now records directory device/inode;
append rejects a different identity and latches writes until re-recovery.

Native C++ regression constructs a valid replacement ordinary directory with
the original history segment, refuses writes without mutating its contents,
restores the original and verifies normal writes after successful recovery.
ESP-IDF LittleFS lacks reliable inode identity and retains path/type checks.
Initial `5b741418` failed formatting-only CI run `40860714630`; corrected exact-SHA `bf8d7fbd64113bb96684eedbc4eb6134172ad10d` passed run `40860928672` (native C++/Python, simulated REST/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1). No physical power loss, owner Wi-Fi, LCD/DS18B20,
OTA, factory reset or soak testing occurred. T06-14/T06-16 and all hardware
and owner-network acceptance gates remain unchecked. No secrets or hardware.

## 2026-10-29 — Refuse regular-directory substitution during history export (software-only)

HistoryStore::visit previously refused a symlink replacement but could accept
a different ordinary directory installed during an export callback. Native
host exports now capture the original directory's device/inode and recheck
identity before every segment, after callback completion, and on success.
ESP-IDF LittleFS has no reliable inode identity and retains type validation.

A native C++ regression installs a different ordinary directory with valid
same-size records while the first segment's callback executes; a mixed
history export must fail after exactly one record, and restoring the original
directory permits a complete two-record read. No owner hardware, secrets,
Wi-Fi, flash, OTA, factory reset or power interruption were used.
T06-14/T06-16 and all physical acceptance remain unchecked. Exact-source `c1fdf7bcac95316b5477eee1ab8c89bbbe05c375` passed GitHub Actions run `40858791895` with native C++/Python tests, simulated REST/WebSocket, headless LVGL, React checks and pinned ESP-IDF v6.1 firmware compilation.

## 2026-10-29 — Refuse missing or substituted history directories (software-only)

HistoryStore now rejects missing/non-directory and host symlinked history
directories during recovery, append and export instead of silently reporting
empty history or writing through a substituted directory. Native regression
renames a populated directory, substitutes a symlink to its original location,
checks append/export/recovery refusal and unchanged history, restores the
directory and recovers, then checks missing-directory export refusal.

Initial source `6ffa567ea4a46d0862ce51ba546f2b0b251bfdf1` failed
C++ formatting in Actions run `40849492146`; corrected source
`2dbbad3faffcd715658a2eec5de491e2edfeca04` passed exact-SHA
Actions run `40849582879` (native C++/Python, simulated REST/WebSocket,
headless LVGL, React and pinned ESP-IDF v6.1 firmware build).
No physical LittleFS, ESP32, sensors, LCD, owner network, OTA, reset or power
interruption was used. T06-14/T06-16 remain unchecked, as do other physical
acceptance gates. TODO remains 114/158 checked; 44 open.

## 2026-10-29 — Fail closed on missing history segments and exclusive rollover paths (software-only)

HistoryStore now refuses to silently recreate an active segment deleted after
successful recovery. New rollover segments use exclusive creation instead of
opening with append mode, preventing pre-created paths or host symlinks from
redirecting writes. Opened file descriptors are verified as regular files;
host append also uses O_NOFOLLOW when available. Native regression covers
pre-created rollover symlinks, unchanged external target, recovery/writer latch,
successful normal rollover, and active-segment deletion.

Exact-source commit `484eb00ebb6e27c39780c4bb9948baed3d434569`
passed GitHub Actions run `40847935409` (C++ formatting, native C++/Python,
simulated REST/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1 build).
T06-14/T06-16 remain open for actual LittleFS exhaustion, power interruption,
reboot and durability verification. No device, owner Wi-Fi, OTA or reset was used.
TODO remains 114/158 checked; 44 items remain open.

## 2026-10-29 — Preserve history during failed rollover writes (software-only)

HistoryStore previously deleted old segments to satisfy the retention limit
*before* writing the replacement record. An ENOSPC, flush or fsync failure
could therefore erase valid historical data without persisting the new
sample. HistoryStore now writes and fsyncs the new record first, then
enforces its logical byte cap by evicting older segments. If post-write
eviction fails, the writer latches closed until recovery. The transient
logical overage is bounded by one encoded history record.

Native storage regression fills four records across two segments, injects
RLIMIT_FSIZE=0 at the fifth record/rollover, checks that all four older
records and the oldest timestamp remain available, then recovers, appends,
and confirms bounded retention and valid chronological ordering.
Exact-source commit `23928ed85d3a0f89da929448a41ede60c2d25d2b` passed GitHub Actions run `40840129656` (native C++/Python, simulated API/WebSocket, headless LVGL, React, formatting and pinned ESP-IDF v6.1 firmware build). This is
host-only evidence, not physical LittleFS-full, power-loss or reboot
acceptance. T06-14/T06-16 and all hardware/owner-network/OTA/reset/soak
gates remain unchecked. No devices or secrets were accessed.

## 2026-10-29 — Event journal substitution and compaction temp safety (software-only)

EventStore now rechecks that the journal is a regular, non-symlink path
before appending or exposing API pages, including after a previously
successful recovery. Compaction no longer opens its .tmp path with
truncating "wb": it uses exclusive creation and refuses a pre-existing
temporary path (including a symlink or interrupted-compaction artifact).
A failed compaction leaves the current journal intact and latches the
writer until successful recovery. Host storage regressions check
post-recovery path substitution, target byte preservation, refusal of a
symlinked .tmp, and subsequent recovery/sequence continuity.

Exact-source `560f998ab2dbb951cb03155eafd0b5497c6e563e` passed GitHub
Actions run `40836865635`: native C++/Python host regressions, simulated
REST/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1 firmware
compilation. This does not establish physical LittleFS power interruption, OTA/rollback, owner Wi-Fi,
five probes/LCD, factory reset, or sustained-load acceptance; T06-14,
T06-16 and all hardware/owner gates remain unchecked. No secrets,
flashing, hardware or destructive reset were used.

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

Initial source `b6c89553` failed only the C++ formatting gate (Actions `40834070502`); the test expression has been simplified to satisfy clang-format. Formatting correction `93c1f4d0` passed formatting and pinned ESP-IDF v6.1 compilation, but storage_recovery failed at the legacy `/dev/full` symlink test (Actions `40834400874`). HistoryStore now deliberately rejects that unsafe path; the host tests have been updated to inject an actual regular-file write/flush failure using `RLIMIT_FSIZE` instead. Corrected source `cf6e9c8c06da1b44faeca5e45382080137ee0f8f` **passed** exact-SHA GitHub Actions run `40835122250` (C++ formatting, native C++/Python storage and diagnostics, simulated REST/WebSocket, headless LVGL, React checks and pinned ESP-IDF v6.1 firmware build). This verifies host-only write/flush failure handling; it does not establish physical LittleFS power-loss behavior. This is
software-only protection, not real ESP32 LittleFS power interruption,
wear measurement, OTA/rollback, owner-network, LCD/probe, physical reset or
sustained-load acceptance. T06-14/T06-16 and all owner/hardware gates
remain unchecked. No credentials or physical devices were accessed.

## 2026-10-28 — Event-journal read-error integrity and existing-link regression (software-only)

After exact-source commit `2ceca9d0a51f13347b2f5189228ea86c56ef95ae`
passed the full pinned ESP-IDF v6.1, native C++/Python, simulated
REST/WebSocket, headless LVGL and React workflow (Actions `40827973869`),
hardened the journal's recovery, compaction-reader and API pagination
paths to fail closed on actual stream I/O errors rather than interpreting a
short read as a valid EOF or interrupted write. Native regression also checks
that a symlink to an existing valid journal is rejected and that the target's
contents remain byte-for-byte unchanged; the earlier dangling-link,
directory and normal first-create tests remain in place.

Exact-source commit `6d41629f811343ab4f30832428ca617f05f08941`
passed GitHub Actions run `40828571625`: native C++/Python tests,
simulated REST/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1
firmware build. No physical
LittleFS power-cut, ESP32-S3, five-probe/LCD, owner Wi-Fi, OTA rollback,
factory-reset or combined-load acceptance is claimed. T06-14/T06-16 and all
hardware/owner prerequisites remain unchecked. No secrets or devices accessed.

## 2026-10-28 — Restore ESP-IDF v6.1 link compatibility (software-only)

The preceding path-safety change (commit `3a3323f5`) passed the host portion
of GitHub Actions but failed the pinned ESP-IDF v6.1 link step: the Xtensa
runtime does not export `lstat` (run `40823488328`, undefined reference).
The recovery path now uses `stat` under `ESP_PLATFORM`, retaining `lstat`
for native host regression of symlink refusal. ESP-IDF LittleFS does not
support symbolic links; regular-file checks and exclusive creation still
apply. This is a compatibility correction, not physical LittleFS or power-loss
acceptance. Exact-head CI is pending for the correction. T06-14/T06-16 and
all hardware/owner gates remain unchecked; no credentials or hardware used.

## 2026-10-28 — Refuse unsafe event-journal paths during recovery (software-only)

EventStore recovery now distinguishes a genuinely missing journal from a failed
read of an existing path. It uses lstat() to reject symlinks and non-regular
files and open(O_CREAT|O_EXCL) with owner-only permissions for first creation;
the new empty file must be fsynced and closed successfully before writes are
enabled. Existing files whose ifstream open fails are preserved rather than
opened with truncating "wb" access. Host regressions cover a dangling symlink
whose target must remain absent, a directory mistaken for a journal, and the
existing normal first-create, reopen and append path.

This is an independent software-only storage safety fix, not proof of physical
ESP32 LittleFS durability, interrupted power, five-probe/LCD testing, owner
Wi-Fi/DHCP, OTA rollback, factory reset or sustained load. T06-14/T06-16
and the remaining hardware gates remain unchecked. Exact-commit CI outcome
must be recorded after the implementation commit is verified. No owner
credentials or physical hardware were accessed; no flashing or reset occurred.

## 2026-10-28 — Require recovery before first storage append (software-only)

HistoryStore and EventStore previously initialized their append-ready latches
to true. A caller that constructed a new writer but forgot recover() could
append to existing persistent data using the default segment/sequence state,
potentially duplicating event sequence IDs or bypassing history rotation.
Both writers now start fail-closed and require successful recovery before any
append. Native regression checks a fresh writer against empty and populated
history/event files, confirming rejected appends preserve on-disk byte counts;
the existing recovery, rollover, deduplication and simulated power-cut suites
still pass. Exact-source `b65849791166169cd78756604b776fe2cb7c0146`
passed GitHub Actions run `40811896227` (native C++/Python, simulated REST/
WebSocket, headless LVGL, React, pinned ESP-IDF v6.1). This is software-only
storage safety; physical LittleFS/power-loss, owner Wi-Fi, sensor/LCD, OTA,
factory reset and combined-load acceptance remain open. No hardware, secrets
or destructive operations were used.

## 2026-10-28 — Preserve malformed partial history headers (software-only)

HistoryStore recovery previously truncated arbitrary short active-segment tails,
including invalid magic, unknown versions and impossible length fields, as if
they were interrupted TMH1/TMH2 appends. It now recognizes the partial
TMH1/TMH2 header progressively and preserves invalid bytes rather than
silently discarding diagnostic evidence. Host regression injects malformed
1–8-byte prefixes and checks byte-for-byte preservation. Existing one/five-
probe interrupted-record tests continue to pass for every byte boundary,
including rollover. Exact-source commit
`0510da535cc4f5c5fd12167e954e72c2b9ead146` passed GitHub Actions run
`40811329826`: native C++/Python, simulated REST/WebSocket, headless LVGL,
React and pinned ESP-IDF v6.1 compilation. This is host-only simulation,
not physical LittleFS/power-loss or reboot acceptance. T06-14/T06-16 stay
unchecked. No owner devices, credentials, flashing, OTA or reset were used.

## 2026-10-28 — Preserve malformed partial event headers (software-only)

EventStore recovery previously truncated short arbitrary journal tails as
interrupted writes, even when the magic, version or declared record length
did not match the current TME1 schema. Recovery now permits truncation only
for a valid prefix of the eight-byte current-format header and preserves
unknown/corrupt prefixes for diagnosis. Native regression exercises all
eight invalid header prefix lengths, byte-for-byte preservation, the 60
valid interrupted-append cuts and future-version fragments. Initial source
`551bbe5d` failed C++ formatting (run `40810743113`); corrected exact
commit `89e9c101993b04ace9c25e8123e16711a14e1b8f` passed Actions run
`40810808184` (native C++/Python, simulated REST/WebSocket, headless LVGL,
React and pinned ESP-IDF v6.1). T06-14/T06-16 and real LittleFS power-loss
acceptance remain open; no hardware or owner secrets were accessed.

## 2026-10-28 — Oversized event-journal integrity guard (software-only)

An externally oversized but CRC-valid event journal previously bypassed the
maximum-byte check during API pagination and could be compacted by a reused
writer, silently discarding records even though recovery rejects the file.
EventStore::page now rejects oversized journals, and compact() refuses them
without changing the file. Native storage regression constructs a three-record
journal under a two-record capacity, confirms unavailable pages and failed
append/compaction without data loss, and verifies recovery remains blocked
until a deliberate host-only truncation restores the configured bound.
After successful recovery, append and sequence continuity work normally.
Exact-commit `6e05d14b9a881c4fa3e0fb128bf76cec84ebc293` passed Actions
run `40806004585`: native C++/Python, simulated REST/WebSocket, headless LVGL,
React and pinned ESP-IDF v6.1 firmware build. T06-14 remains open for actual
LittleFS, real retention, reboot and power-loss acceptance. No owner hardware,
Wi-Fi/admin credentials, flashing, OTA or factory reset were accessed.

## 2026-10-28 — Fail-closed reused storage recovery and compaction (software-only)

A reused HistoryStore or EventStore could retain write permission from an
older successful recovery even after a later recovery rejected corrupt data.
EventStore also left writes enabled after failed journal compaction. Both
recovery methods now revoke append permission before inspection and restore it
only after complete successful validation; failed event compaction also latches
the writer until recovery succeeds. Native storage regression repairs files
after failed recovery and verifies that appends stay blocked until revalidation;
a CRC-valid out-of-sequence event journal likewise remains blocked after its
compaction failure until repaired and recovered. Exact-commit
`27e00f04198f41de59d542265d5eb6b6ad174e13` passed GitHub Actions
run `40804958259`: native C++/Python tests, simulated REST/WebSocket,
headless LVGL, React checks and pinned ESP-IDF v6.1 firmware compilation.
T06-14 remains open for physical LittleFS, reboot, real retention and power-
loss acceptance. No hardware, owner credentials, flashing, OTA or reset used.

## 2026-10-28 — Fail-closed event pagination beyond page boundaries (software-only)

The production EventStore::page reader previously stopped after the first
record beyond the requested page size. A CRC-valid duplicate or descending
sequence (or an incomplete write) later in the journal could therefore leave
the first API page marked available, despite the journal failing recovery.
Pagination now scans and validates the entire bounded journal while retaining
only the requested page and has_more cursor semantics. Native storage regression
covers a valid three-event one-item page, a CRC-valid duplicate in the third
record, and a torn tail beyond the first page. Exact-commit `abafc225ec6599c88a4113fe258bba054215ad5c` passed
GitHub Actions run `40798587386`: native C++/Python storage and diagnostic
tests, simulated REST/WebSocket, headless LVGL, React and pinned ESP-IDF v6.1
firmware compilation. This does not qualify physical LittleFS,
power interruption, owner Wi-Fi, LCD/probes, OTA, reset or device load.
No hardware, credentials, flashing or destructive operations were used.

## 2026-10-28 — Event journal sequence integrity and full progress restoration (software-only)

The event-journal API pagination and compaction reader now reject
CRC-valid repeated or descending sequence IDs, consistent with recovery's
existing monotonicity check. Native regression writes two-record journals
with recalculated valid CRCs, verifies pagination fails closed even at the
one-item page boundary, confirms compaction refuses tampered records, and
accepts legitimate retention gaps. Intermediate CI `515d2058` (run `40793164565`) failed C++ formatting;
corrected exact-SHA `5133c2d61f66302de5a0b879414b7c108f18f4c3` passed
Actions run `40793255641` (native C++/Python, simulated API/WebSocket,
headless LVGL, React and pinned ESP-IDF v6.1). This checkpoint also restores the entire historical PROGRESS.md
record from the verified parent after a bounded-read write accidentally
omitted its older lines; historical entries are preserved verbatim below.
No physical LittleFS, device, credentials, OTA, reset or power loss was used.

## 2026-10-28 — Fail-closed storage append recovery (software-only)

The production history and event-journal writers now latch after a buffered
write, flush, fsync or close failure. A later append is rejected until a
successful recovery pass inspects and, where safe, repairs a torn tail.
The latch is cleared only by successful recovery, including empty/missing
journal initialization. Native storage regression injects ENOSPC using
`/dev/full`, restores the original persisted files, verifies retry remains
blocked before recovery, then verifies two intact history buckets and
continuous event sequences after recovery. Exact-SHA
`29ccc5707fe89ac700931fb37ec26904af812284` passed Actions run
`40792383126`: native C++/Python, simulated REST/WebSocket, headless LVGL,
React verification and pinned ESP-IDF v6.1 compilation. This is a software-
only T06-14 improvement; physical LittleFS power interruption (T06-16),
flash retention, OTA, owner Wi-Fi, LCD/probes and reset acceptance remain open.
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


## 2026-10-29 — Connected ESP32-S3 retest and history journal failure

The ESP32-S3 appeared as `/dev/ttyACM0` (`303a:1001`, Espressif USB JTAG/serial). A serial smoke capture using ESP-IDF v6.1 confirmed the target boots version 0.1.0, reports 16 MiB flash and 8 MiB PSRAM, reaches `READY`, reports zero sensors with the 1-Wire bus and backlight disabled, and maintains approximately five-second health cadence without panic. No LCD or probes were connected.

The authenticated USB-loopback API test confirmed static gzip assets, anonymous and forged-host rejection, authenticated status, invalid-OTA rejection before flash access, a real radio scan, authenticated live snapshots at 5,000 ms cadence, forbidden WebSocket data-frame closure, four-client admission with the fifth rejected (503), HTTP status under four-client load, and history query bounds. It then failed the durable reboot-event assertion. A separate authenticated read-only query reported the event journal unavailable with no events. A subsequent reboot/smoke log reported `mount=ESP_OK healthy=0`, `event_log_healthy=0`, and `Event journal write failed`; the smoke still passed its health/no-fabricated-sensors/no-panic checks. The cause is not established. No history formatting, factory reset, Wi-Fi credential submission, OTA, or peripheral wiring was performed.

Preserve the existing history partition. Diagnose the LittleFS/history/event failure before further persistence or destructive acceptance tests. T06-14/T06-16 remain open; T07-10 remains open because device TCP slow-client cutoff was not established. LCD, physical probes, owner-network onboarding, OTA/rollback, destructive reset, and long soak acceptance remain untested.


## 2026-10-29 — ESP32-S3 history recovery and protected reset acceptance

The connected ESP32-S3 was authorized for destructive qualification. The prior history partition (`0x820000`, 4 MiB) was erased with `esptool erase-region`; the app, bootloader, partition table, OTA metadata and NVS were preserved during that operation. This intentionally destroyed the prior history. A private pre-erase readback mounted with `littlefs-python`; the five 60-byte event records passed CRC and increasing-sequence checks, and the production host `EventStore::recover()` accepted the extracted journal. The earlier device-only recovery failure therefore remains unexplained.

Added guarded, ESP-only recovery diagnostics that log source line and `errno` if `HistoryStore::recover()` or `EventStore::recover()` rejects storage. The ESP-IDF v6.1 build passed (application image `0x1e8aa0`, 52% OTA-slot headroom) and `idf.py app-flash` wrote only the app partition. Thereafter multiple bare-board smoke/API runs reported healthy history/event stores and passed. No diagnostic rejection line was observed after app-only reflash; do not infer a root cause. The board boots `READY`, zero sensors, 1-Wire disabled, backlight disabled, and approximately 5-second health cadence; no LCD/probes were attached.

The authenticated device API suite passed after the fresh history recovery and again after both reset modes: gzip assets, enrollment/login and cooldown, anonymous/forged-host/CSRF rejection, invalid OTA image rejection before flash access, real radio scan, authenticated WebSocket cadence and forbidden-frame close, four-client cap/fifth-client 503, healthy HTTP under four sockets, bounded history queries, durable reboot event, and safe reboot. With `erase_history:false`, factory reset cleared NVS but retained the prior 11 events through setup retrieval and test-admin re-enrollment (16 events after subsequent testing). With `erase_history:true`, reset cleared the prior 17-event journal and returned with healthy fresh storage; after re-enrollment the new journal had five events starting at sequence 1. The test administrator remains marked test-managed in the private mode-0600 `.build/device-admin.json`; setup credentials remain private in `.build/device-setup.json`.

A guarded candidate-failure run used a random nonexistent SSID while station credentials were blank: the candidate was accepted, overlap rejected with 409, association failed, and SoftAP setup stayed available without saved credentials. This does not test owner-network retention, successful DHCP/NTP, or phone onboarding. T09-15 is complete. T05-04 remains open for preserving an existing network; T06-14/T06-16 remain open for rollover, wear, export concurrency, full-media/corruption, power interruption and long-run durability; T07-10 remains open for actual slow-client cutoff and disconnect/reconnect recovery. LCD, probes, phone onboarding, OTA/rollback, combined load and soak remain untested.
