# Code Review Remediation Specification

**Status:** Proposed implementation baseline; no fixes implemented by this document<br>
**Date:** 2026-10-30<br>
**Review baseline:** `290640cfe801cffe7829103630458039bf7cf8f2`<br>
**Companion checklist:** [CODE_REVIEW_REMEDIATION_TODO.md](CODE_REVIEW_REMEDIATION_TODO.md)<br>
**Evidence:** [Code review and complete task audit](CODE_REVIEW_TODO_2026-10-30.md)

## 1. Purpose and scope

Resolve findings F1–F7, the additional UI and maintainability observations, and the verification gaps identified in the review. This specification supplements [SPEC.md](SPEC.md); the existing [TODO.md](TODO.md) remains the canonical tracker for original system and physical acceptance requirements. Every remediation must trace to a review finding, observation, or audited acceptance gap.

Retain ESP-IDF v6.1 and its pinned revision, five managed ROM identities, one shared powered 1-Wire bus, five-second sampling, five-minute history, Celsius storage, read-only LCD, and the owner-approved local HTTP model. Optional features in the original backlog remain optional. Hardware results require hardware evidence; a software remediation pass does not qualify a release.

## 2. R01 — Safe retirement and replacement of managed probes (F1)

Provide an explicit authenticated web/API operation to retire a managed ROM and free its slot. Require CSRF/Origin/Host protection, current configuration revision and clear confirmation. Disabling monitoring alone must continue to preserve its mapping. Define whether connected probes may be retired and prevent immediate automatic rediscovery from silently undoing the operation.

Persist the retirement before reporting success. Preserve historical records and event attribution under the original ROM; never relabel old data as the replacement. A newly admitted replacement receives neutral defaults, with no automatic transfer of calibration or alarm limits. Configuration failure must leave a recoverable, truthful state under R04. Document what happens if the retired ROM later reconnects.

Acceptance: fill five mappings, remove one probe, retire its mapping, admit a different ROM, restart, and verify the four unchanged mappings, replacement defaults, released capacity and original history attribution. Cover stale revisions, concurrent edits, unauthorized requests, failure during persistence, and reappearance of the retired probe. Begin with production host logic and fake bus/storage adapters; retain a separate physical replacement check.

## 3. R02 — Bounded discovery and visible excess devices (F2)

Separate bounded discovery observations from the five managed read bindings. Discovery must prioritize managed connected ROMs when selecting scratchpad reads, so adding a lower-sorting sixth ROM cannot displace a known probe. Admit unmanaged probes only into available managed capacity.

Publish discovered/managed counts and an excess-device diagnostic through the versioned status contract and web status. When an enumeration cap prevents an exact count, report a lower bound or an explicit incomplete-scan flag. Never present a capped count as an exact total. Keep enumeration, conversion, reads and memory bounded. Define overflow clearing and replacement selection consistently with R01.

Acceptance: exercise the production discovery-to-sampling integration with zero through six probes, discovery permutations, a lower-sorting sixth ROM, duplicates, invalid families/CRCs, removal/reconnection, reboot, and more devices than the scan cap. Five managed connected probes must remain fresh and excess status must become visible and later clear. A registry-only test is insufficient; real bus validation remains separate.

## 4. R03 — Preserve historical gaps through downsampling (F3)

Derive missing intervals from original per-ROM/per-boot records before information is lost by reduction, or retain equivalent discontinuity metadata. Preserve the exact missing five-minute interval in the reproduced 0/600,000 ms case even when `max_points=1`. Distinguish missing buckets from a partially sampled bucket; never invent the location of individual missing samples inside a partial bucket.

Preserve count-weighted average, extrema, boot boundaries, timestamp quality and query selection. Keep both points and gap metadata bounded with documented limits. When a limit omits coverage, expose truncation explicitly; the UI must not imply continuous coverage through omitted intervals. Keep the existing meaning of `max_points` documented, and version schemas/generated clients if the response contract changes.

The history chart must consume the gap contract and render missing intervals visibly without joining or implying measurements through them. CSV continues to export original five-minute records with existing validity metadata.

Acceptance: test gaps within one reduced group and across groups, multiple ROMs, disabled/disconnected intervals, partial buckets, reboots, UTC changes, one-point budgets, and 1h/24h/7d/30d ranges. Verify response limits and frontend rendering, including gap-metadata exhaustion. Retain ordinary query/CSV and empty-history regressions.

## 5. R04 — Truthful durable sensor configuration (F4)

Expose sensor-settings load/commit health and writability to the API and editor. Preserve corrupt or unsupported NVS data for recovery. Reject persistent edits while initialization has made the sensor store read-only; do not publish an apparently successful durable update from RAM alone.

Give asynchronous mutations an observable outcome that distinguishes queued, committed/applied and failed. Stage changes against an authoritative revision, commit before durable success, and preserve the last committed configuration on failure. A restart at any stage must recover a valid old or new configuration without transferring settings between ROMs. Apply the same rules to retirement/replacement. Keep sampling active and bound queued work.

The editor must show a clear persistence/recovery error, disable unavailable edits, and report success only after commit confirmation. A changed live revision alone is not sufficient evidence. No volatile-only editing mode is introduced by this remediation.

Acceptance: inject missing keys, corrupt/unsupported records, NVS open/set/commit failures, later save failures, stale revisions and reboot boundaries through the service/API/editor integration. Verify preserved original bytes, honest status, no false success and continued monitoring. Keep fault injection isolated from the user's normal settings.

## 6. R05 — Consistent configured timezone (F5)

Use the selected supported IANA timezone for synchronized history and event timestamps, matching LCD presentation. Keep timezone settings available to both pages and apply successful changes without reloading the application. Label the selected zone clearly; do not silently substitute the browser zone when configuration is unavailable.

Store and transmit UTC unchanged. Unsynchronized records retain boot ID and elapsed time. Use timezone-aware browser formatting and reconcile the user guide with this behavior.

Acceptance: render a Los Angeles-configured device in a Tokyo-configured browser, test both DST transitions and UTC, change the zone while pages are open, and cover unavailable settings, reboot and unsynchronized records.

## 7. R06 — Overall JSON body deadline (F6)

Apply a monotonic deadline of at most five seconds to receiving ordinary JSON request bodies, including unauthenticated login/enrollment. Each receive wait must be bounded by the remaining deadline. Close timed-out or incomplete request connections so unread bytes cannot be interpreted as another request. Preserve existing body-size, content-type, JSON, authentication and CSRF checks.

This limit governs receipt, not the separately measured password hash operation. Retain the OTA route's independent upload budget. Timeouts must release handler resources and allow other requests and queued WebSocket broadcasts to resume. Restore any modified socket timeout correctly before connection reuse, or close the connection.

Acceptance: send a body one byte every two seconds, keeping each individual receive below its existing timeout. The request must terminate within five seconds plus a documented scheduler tolerance of at most one second. A concurrent healthy request must recover within that same bound when the body receiver is the only blocker. Check EOF, partial/pipelined input, deadline-boundary input and normal bodies. On the ESP32, also verify health/acquisition cadence and WebSocket recovery; use an isolated test account and bounded test duration.

## 8. R07 — Accurate contributor and project documentation (F7)

Revise contributor guidance to describe current firmware/web/contracts/tools/tests, actual formatting rules, pinned build/verification commands and device limitations. Preserve relevant repository-specific instructions and hardware hold points.

Reconcile affected specifications, user guides and current handoff/status summaries after fixes. Replace the claim that only hardware work remains with an evidence-based status. Keep original task IDs, distinguish implementation from physical acceptance, move lengthy historical detail into linked progress records without losing evidence, and repair literal escaped-newline artifacts. Optional work must remain separate.

Acceptance: commands and paths match the repository; links resolve; checked tasks affected by the review are reconciled with their actual acceptance evidence and remaining blockers. Historical reports retain their reviewed revision and are not rewritten to imply fixes existed then.

## 9. Additional review observations

### R08 — Honest dashboard history state

Replace unconditional “No history available yet” text with a bounded, real history availability state, or neutral navigation text that makes no data-availability claim. If loading history, distinguish loading, populated, empty and unavailable results, with boot/range changes handled safely. Test populated, empty, unavailable and unsynchronized states.

### R09 — Authentication connection retry

Offer an in-page retry after the initial auth-state request fails. Show clear connecting/error/retry state, avoid overlapping requests, and ignore stale/unmounted results. Preserve credentials only as required for the active form and never persist them in browser storage. Test failure followed by recovery, repeated retry and unmount/session-expiry races.

### R10 — Responsive BOOT progress and independent display updates

Decouple display/status refresh from the five-second acquisition loop, using a thread-safe published model and the existing LVGL locking rules. Display progress at least every 100 ms while holding BOOT, with a target visible latency no greater than 200 ms. Apply configured view timing independently of sensor conversion delays. Keep five-second sensing and five-minute logging unchanged; no flash reads/writes per progress update. Test scheduling/hold progress with a fake clock and headless rendering; measure physical latency only after the LCD is connected.

### R11 — Clock diagnostics and stale-data integration

Expose last successful SNTP sync time/age and honest unknown status; monotonic age must remain meaningful across wall-clock jumps and temporary Wi-Fi loss. Trace the reviewed unused registry-expiry helper and establish how a stalled acquisition is detected, without allowing concurrent unsynchronized registry mutation. Ensure network and LCD consumers do not keep presenting an old reading as fresh when acquisition stops. Tests must exercise actual runtime boundaries with fake time, not only call a disconnected helper. Preserve current unknown-UTC and boot-relative history behavior.

### R12 — Maintainable service boundaries

Extract focused, testable request validation/configuration/history handling from the large HTTP dispatcher where useful for R01–R06. Separate button/recovery behavior from USB diagnostic bridge code. Preserve the authorized physical USB recovery/setup capability and existing security gates. Prefer scoped refactoring alongside fixes; avoid an unrelated architecture rewrite. Verify behavior through production service integration tests and existing suites.

## 10. Verification gaps, physical gates and completion

Create an explicit page/state coverage matrix for Dashboard, History, Sensors, Alerts, Settings and Setup/Auth. Include zero/one/five probes, long names, shared bus failure, disconnected/disabled probes, empty history, unknown time, lost WebSocket and storage failure. Mark combinations inapplicable with a reason. Use fresh 360 CSS px and at least 1280 CSS px browser checks for overflow, keyboard access and status readability. Exercise production adapters for discovery, persistence and HTTP body handling instead of relying exclusively on policy mocks.

Track representative populated-storage query/export/append latency and memory under concurrent traffic; a 120-second mostly empty-store test does not establish long-run performance. Keep physical identification, LCD electrical/visual checks, real probes/pull-up, secured onboarding/phone, router-offline recovery, populated-history OTA comparison, physical power loss and 24–72-hour combined soak linked to their original task IDs. Run independent software work while these prerequisites are unavailable.

Remediation completion requires evidence for R01–R12, appropriate host/frontend/service regression checks, pinned firmware compilation, and review of the resulting diff. Record the tested revision, commands/results, simulated versus physical status, remaining risks and exact external prerequisites. Original release approval remains dependent on its full physical qualification and owner signoff.
