# Review Audit Fixes — Specification

Status: proposed implementation and acceptance plan; no fixes are claimed complete.
Created: 2026-11-12.
Companion tracker: [REVIEW_AUDIT_FIX_TODO.md](REVIEW_AUDIT_FIX_TODO.md).

## 1. Purpose and baseline

Close the nine findings from the code review of `REVIEW_FIX_TODO.md` at commit
`b20a4f9265a2a98b3d654c1d22d28a5204384254`, and resolve the acceptance gaps
identified by its task audit. These documents supplement
[REVIEW_FIX_SPEC.md](REVIEW_FIX_SPEC.md) and
[REVIEW_FIX_TODO.md](REVIEW_FIX_TODO.md); they do not silently replace their
requirements or mark their tasks complete.

The review ran 41 passing host tests, 51 passing Python tests, one passing
headless display test, and passing simulator checks. The full web run passed
229/230 tests; all 11 Settings tests passed on a focused rerun. That rerun does
not establish a green full gate. No new physical acceptance or final firmware/CI
verification was performed during the review.

The detailed local audit was written to
`.build/review-fix-audit-2026-11-12.md`. That ignored artifact is not required to
use this specification: findings and outstanding acceptance are reproduced below.
All file locations describe the baseline; line numbers may change during fixes.

## 2. Scope, compatibility, and evidence rules

- Keep the ESP32-S3 target, pinned ESP-IDF v6.1, current partition layout,
  public read model, owner-only changes, and documented HTTP confidentiality
  limitation. Do not restore the removed owner code or the superseded eight-client
  WebSocket limit; the current limit is four, including an owner reserve.
- Preserve binary history/event/settings formats, JSON schema v1 and CSV column
  layout for these fixes. Internal result types and test seams may change.
  Any necessary wire or storage incompatibility needs an explicit version and
  migration decision before implementation, recorded with the change.
- No production-store corruption, flash erasure, partition changes or automatic
  release publication is part of this plan. Use temporary host stores and test
  doubles for fault injection. Device tests use the existing setup-file workflow;
  credentials and raw private setup data stay out of Git and logs.
- Write a regression that exposes each confirmed defect before fixing it.
  Exercise the production caller as well as the domain helper. Do not weaken or
  delete behavior assertions merely to make a test pass.
- Record source SHA, build/configuration, command, expected and observed result,
  elapsed times, and limitations. Label host, simulator, browser and physical
  evidence separately. A pending physical gate stays open even when code passes.
- Follow the wiring document's electrical hold points. New electrical measurements
  remain with their original tasks; readable pixels do not establish safe wiring.

## 3. Finding-to-work mapping

| Finding | Severity | Required work | Related original tasks |
| --- | --- | --- | --- |
| F1 | P1 | Calibration hold lifecycle (§4) | R11-01/02/04 |
| F2 | P1 | Honest CSV completion (§5) | R02-05/09, R04-07 |
| F3 | P2 | Recoverable settings saves (§6) | R04-08 |
| F4 | P2 | Cooperative bounded history work (§7) | R02-05, R07-04, R10-03 |
| F5 | P2 | Adaptive chart ticks (§8) | R12-05 |
| F6 | P2 | History cache identity (§9) | R06-01, R12-04 |
| F7 | P2 | Allocation-safe history JSON (§10) | R05-03 |
| F8 | P3 | Deterministic Settings test (§11) | R06-08, R12-06 |
| F9 | P3 | Consistent current documents (§12) | R09-08, R10-11/13 |

## 4. F1: Keep calibration hold renewal alive for the whole flow

Baseline: `web/src/features/calibration/IceCalibrationDialog.tsx` uses one
`clearTimers()` helper for hold renewal and measurement timers. Reaching a steady
result or stopping cancels renewal; retry does not restore it. Alerts and history
can resume with the probe still in ice.

Required behavior:

1. A successful initial hold requests 15 minutes. Renew every five minutes for
   the entire open flow, including instructions, measurement, result, retry and
   save confirmation. Each firmware hold remains RAM-only and capped at 20 minutes.
2. Stopping measurement clears only its sampling, give-up and revision-check
   work. A retry maintains exactly one renewal schedule; repeated renders must
   not create duplicate intervals or requests.
3. Done, Cancel, Escape, navigation and unmount stop renewal and request the
   five-minute tail once per flow. Serialize or otherwise order pending work so
   an old initial/renewal request cannot extend the hold after the final tail.
   A delayed initial response must not install a timer after unmount.
4. On a failed renewal, stop treating the alert pause as assured. Show an
   actionable error and prevent proceeding as if calibration is protected. A
   retry must re-establish the hold before measuring. A failed tail request must
   not produce an unhandled rejection or an indefinite local timer.
5. Preserve the existing arithmetic, steady-reading rules, disconnect/bus-fault
   safeguards, revision/ETag checks, unchanged-offset case and unit conversions.

Acceptance: fake-time dialog tests keep a steady-result flow open beyond 20
minutes; cover ten-minute give-up then retry, faults/revision stops, failed
renewal, delayed initial/renewal responses and each exit. Assert exact hold/tail
ordering and no post-unmount interval. Host tests still prove firmware expiry and
the cap if the browser disappears. Physical bath acceptance is separate (§13).

## 5. F2: Never report an incomplete CSV export as complete

Baseline: `HistoryService::visit()` discards `HistoryStore::visit()`'s skipped
segment count. `HttpApi::serve_history()` then sends a normal final chunk.

Carry a structured internal outcome from the store through the service to the
HTTP exporter: complete, skipped/corrupt data, deadline/cancellation, storage
failure and delivery failure must remain distinguishable. A boolean success must
not erase incomplete status. JSON queries may return valid partial data with
`truncated: true`; CSV cannot silently omit damaged segments.

Use the existing compatible HTTP failure behavior: if incompleteness is known
before headers, return an actionable 503; if streaming has begun, close the
connection without a successful terminating chunk. Do not append pseudo-data,
extra CSV columns or an unconsumed success trailer to announce failure. The client
and device test must detect transport failure. Do not claim all downloads can be
made atomic after bytes have already reached the browser.

Keep healthy exports complete and bounded in memory. Keep corrupt evidence on
disk and preserve existing active-segment and changed-file refusal behavior.

Acceptance: production-path export tests with a corrupt first, middle and last
completed segment; a corrupt active segment; no matching records; healthy data;
and a client send failure. Check HTTP status or unsuccessful termination, not
only the store's return value. Assert no normal final chunk on incomplete output.
JSON for the same skipped data must remain valid and truncated.

## 6. F3: Allow another edit after a transient NVS failure

Baseline: `SettingsService::save()` remains writable after transient errors, but
both `firmware/main/main.cpp` save-failure paths permanently set the API's
sensor-settings writable flag to false.

Treat corrupt/unsupported loaded storage and transient open/set/commit failures
as distinct states. Loaded corrupt data remains preserved and read-only. A failed
user edit must restore the last committed configuration, must not advance its
applied revision or report success, and must allow a later valid edit to attempt
storage again without reboot. Automatic discovery/calibration persistence must
also recover; any background retry must be bounded/backed off rather than writing
on every sampling cycle indefinitely. Acquisition and display continue.

Use the service's authoritative writability state at the caller/API boundary.
Keep asynchronous committed/applied reporting honest; the browser must not show
"committed" for a failed candidate. Preserve existing If-Match/pending-generation
conflict handling and avoid permanent pending updates after rejection.

Acceptance: an integration seam around the actual main-loop save decision and
API publication injects open, set and commit failures, observes rollback, then
allows a successful second edit without restart. Cover both explicit updates and
automatic persistence. Assert durable contents, revision, writable/pending flags
and resumed reads. Retain the corrupt-load read-only test. A passing
`SettingsService` retry test alone does not close this finding.

## 7. F4: Make history cancellation bound work and lock occupancy

Baseline: a false visitor stops delivery but continues full storage validation.
The service's three-second query and fifteen-second export budgets therefore do
not stop the scan or release its mutex on expiry.

### 7.1 Iteration and integrity

Introduce a cooperative cancellation/deadline mechanism distinct from normal
visitor pagination. Check it before opening each segment, during bounded reads
and hashing, during record validation/delivery, and before final revalidation.
Check at least once per record and per read/hash chunk no larger than 4 KiB.
After cancellation, do not start another segment or finish scanning the remaining
retained data. Return an explicit non-complete outcome and release locks and
admission slots through all exits.

Do not remove file-integrity defenses to improve timing. Normal completion must
retain applicable validated-snapshot, size/fingerprint and host file-identity
checks. An incomplete/cancelled traversal must never be labeled fully validated.
Keep ordinary pagination semantics explicit for callers that still require
validation after delivery stops. Reuse validated data where possible instead of
unnecessary repeated decoding, with bounded memory suitable for the device.

### 7.2 Service and performance

Keep the query budget at three seconds and export budget at fifteen seconds,
measured monotonically from service work start. Separately report mutex wait,
execution, response delivery and total request latency; time spent waiting for
admission is not hidden as scan performance. A deadline cannot preempt an already
blocking filesystem/socket primitive: document and measure that residual bound,
plus cleanup time, rather than claim hard real-time cancellation.

JSON deadline results are valid partial responses with `truncated: true`, or an
explicit unavailable response if no valid result can be produced. CSV uses §5.
History workers, writers and public limiter counts recover after timeout,
cancellation, allocation failure and disconnected clients.

Retain the segment range index and verify the original R10-03 requirement on a
full representative store, including short and full-window queries. Record every
sample, not only medians. The original "well under one second" objective is not
met by the recorded 1.43–1.93-second broad queries. If it cannot be achieved,
record the measured limit and seek an explicit owner-approved revision; do not
silently reinterpret it as applying only to short ranges.

Acceptance: fake clock and instrumented storage exercise the real traversal and
service path, including slow validation with no delivered records, corruption,
deadline during hashing, pagination, out-of-range segments and writer contention.
Prove no additional segments are read after cancellation and resource accounting
returns to baseline. Host full-budget fixtures verify range and completeness;
device profiling establishes actual latency and continued status/live/logging.

## 8. F5: Give short charts readable time labels

Baseline: `web/src/features/chart/scale.ts` uses four-hour local boundaries for
every UTC window up to two days. A one-hour view from 13:00 to 14:00 Los Angeles
can have zero ticks.

Select spacing by displayed span and available label density, with sub-hour,
hour and multi-hour intervals. For any positive valid window, emit at least one
usable label; aim for two or more when space allows, using an endpoint fallback
if there is no interior boundary. Ticks must be finite, ordered, unique by
position and inside the window. Avoid zero-step loops and excessive work for
long windows. Keep week/day scales useful and preserve uptime labels.

Honor the configured timezone, including fractional-hour offsets and DST
transitions. Repeated local times around a DST fold must be distinguishable when
they both appear; do not invent a nonexistent local hour. Narrow-screen thinning
must retain useful labels rather than hide every one.

Acceptance: test the reproduced `2026-01-01T21:00Z`–`22:00Z` window in
`America/Los_Angeles`, smaller zooms, 24-hour/seven-/30-day ranges, UTC, fractional
zones, spring/fall DST, invalid/zero windows and unsynchronized uptime. Browser
checks at 360/390 and 1280 px verify readable History and Dashboard axes.

## 9. F6: Bind cached chart data to its original identity and axis

Baseline: Dashboard stores `{key, value}` but uses the value regardless of the
current boot/synchronization/range key and suppresses refresh errors.

An accepted result must carry the request's boot context, UTC/uptime basis,
range and original axis bounds. On boot, basis or selected-range changes, clear
incompatible data while requesting a new answer. Never place previous-boot uptime
records on the new boot's axis or reinterpret uptime values as UTC. Cancel/ignore
late responses belonging to an obsolete request.

For a same-context periodic refresh failure, retaining the old chart is allowed
only with a visible stale/error notice and its original valid bounds. A refresh
counter alone need not invalidate otherwise compatible cached data. Distinguish
live-snapshot freshness from history freshness, and show an actionable empty/error
state when there is no compatible result. Preserve one history query per range
change/five-minute refresh, independent of five-second live updates.

Acceptance: Dashboard tests cover reboot with overlapping old/new uptimes,
first clock synchronization, range change, failed refresh in each case, a late
old response and successful recovery. Assert axis bounds and rendered series as
well as fetch counts. Retain HistoryView's existing key protection.

## 10. F7: Build schema-valid history JSON under allocation failure

Baseline: `HttpApi::serve_history()` does not immediately check the required
`gaps` array allocation; zero-gap responses can omit it without truncation.

Check creation of the root, all required scalar fields and both required arrays.
If the required envelope cannot be built or serialized, return an explicit 503
or unsuccessful transfer using a bounded fallback that does not depend on the
same failing allocator. Every successful JSON response must satisfy
`contracts/history.schema.json`, including `points`, `gaps`, `time_basis` and
`truncated` even for empty data.

A point/gap row allocation failure may yield a valid subset only if all included
rows are complete and `truncated` is successfully set true. If that cannot be
guaranteed, fail the request. Never attach half-built rows, double-free JSON
objects or report a successful complete result after failure. Release history
mutex/admission resources regardless of response construction outcome.

Acceptance: allocator fault injection through production response construction
covers every envelope allocation, empty zero-gap data, nonempty points/gaps,
mid-row failure, truncation update and serialization. Validate every 200 body
against the schema and assert failures do not consume a limiter slot permanently.
Tests must exercise the builder used by the HTTP handler, not a parallel test
implementation.

## 11. F8: Remove polling order from Settings save assertions

Baseline: `DeviceSettings.test.tsx` checks the last fetch, which can instead be a
five-second `/clock` poll under full-suite scheduling.

Route mocks by URL and method. Find the actual settings PATCH and assert its
If-Match header, intended values, absence of response-only fields and applied
callback. Unrelated clock polling may occur before, during or after a save
without changing the result. Control timer advancement where needed and restore
all timers, mocks and pending component work after each test.

Acceptance: deliberately interleave a clock poll with save completion; the test
still verifies the PATCH and would fail for an incorrect payload/precondition.
Run the focused file and the full web suite. One additional full run under the
reproduced polling interleaving is appropriate; repeated passing runs without
the regression scenario are not a replacement for it.

## 12. F9: Reconcile current guidance and tracker status

Update `HISTORY_FORMAT.md` and other current guidance, including the relevant
`DEVELOPMENT.md` export instructions, to match public rate-limited CSV access.
Describe deadlines and incomplete-transfer semantics as implemented after §§5/7.
Do not overstate atomic downloads, full-store speed or physical acceptance.

Recompute numbered task totals in `TODO.md` and reconcile every current summary,
not just T11-09. At the reviewed baseline the count was 128 checked and 17 open
of 145 numbered tasks; acceptance gates are not numbered tasks. Recount after
any tracker changes instead of hardcoding that baseline count.

Cross-check current specs, API contract, user/developer guides, release notes
and both remediation trackers for access, removed owner code, four-client cap,
font delivery and validation claims. Preserve dated historical records as history;
label dated measurements instead of presenting them as the current build.
Annotate affected original tasks with fix/evidence IDs only when their required
criteria are met; a code fix does not close its separate hardware gate.

Acceptance: reviewer compares current guidance to `route_access`, the limiter,
HTTP exporter and actual test results; an independent numbered-ID recount agrees
with every current summary. The new documents must remain usable without the
ignored local review report.

## 13. Acceptance gaps carried forward from the audit

These are evidence requirements, not additional confirmed code defects. Existing
recorded evidence can be cited, but missing portions cannot be inferred from it.

| Gate | Related original work | Required closure evidence |
| --- | --- | --- |
| H01: Calibrated logging and bus recovery | R01-07 | At least 30 minutes of calibrated logging with every expected complete bucket accounted for; controlled multi-probe bus unplug produces one bus fault, no duplicate per-probe storm, and recovery restores an above-limit probe's alarm without an extra persistence wait. |
| H02: Auth recovery and independent clients | R02-01/08 | Isolated corrupt-auth recovery proves Wi-Fi/history preservation; two actual client devices exercise wrong-login load, live/dashboard continuity and owner sign-in from the second address. Preserve or back up state before any controlled recovery test. |
| H03: LCD timing and setup return | R03-05/08, R08-06 | Watch the physical LCD, measure setup appearance against the 200 ms criterion, verify wrong-Wi-Fi setup/readings/alert transitions and AP expiry, and observe its active conflict-renamed local address. Serial view-change logs alone are insufficient. |
| H04: Route handler boundary | R07-06 | Cover every registered route for anonymous/session/CSRF policy through production dispatch/handler tests. For dangerous routes, use a non-destructive handler seam that proves refusal before side effects; do not send a destructive accepted request merely for coverage. |
| H05: Public phone/load behavior | R07-08 | Real phone without a session reads live/history/alerts, threshold editing requires sign-in, and repeated public history load does not stall the dashboard; record rates, latency, WS gaps and heap. |
| H06: Cross-platform mDNS | R08-05/06 | Test available owner Linux, Windows, macOS/iOS and Android clients, rename without reboot and conflict recovery. Record OS/resolver details and IP fallback. Missing platforms remain explicitly unverified until tested or owner disposition is recorded. |
| H07: Retention and range performance | R10-03/08 | One-to-five-probe encoded sizes and full-budget retained ranges, including rollover/eviction, with oldest/newest timestamps and method; distinguish accelerated host fixtures from device observations and forecasts. Run short/broad history and CSV load profiles after §§5/7. |
| H08: Long resource soak | R10-10 | Instrument heap/largest block, PSRAM, clients, display drops, LVGL pool/high-water marks, flash-write counts and Wi-Fi traffic. Soak longer than 24 hours and long enough to see trend-cache fill/plateau; quantify drift and distinguish expected bounded fill from a leak. |
| H09: Hold API and journal compatibility | R11-01 | Connected-probe 409 and 1–20-minute boundary/refusal cases; older reader skips new calibration event kinds and continues to later known records. New reader recovering old data proves the opposite direction only. Use copied journals or an isolated compatibility fixture; record any board check separately. |
| H10: Real ice calibration | R11-04 | Real bath reaches 0.0 ± 0.1 °C, offset stored on probe and retained after restart, no bath-induced low alarm while held, history gap, physical CAL label, start/end events and the five-minute tail. Include the extended-open flow from F1. |
| H11: Complete UI quality pass | R12-07 | Review all page loading/empty/error states for next steps, keyboard focus, accessible names, reduced motion and contrast. Inspect recorded six-page comparisons in both themes at 390/1280 px; repeat changed views and overflow checks at 360/390/768/1280/1600 px. Record exceptions instead of marking a partial pass complete. |
| H12: Offline device and phone UI | R12-08 | With internet genuinely unavailable, all pages, local Roboto Flex and licence load from the board; real phone bottom navigation works. Record bundle size and measured firmware fit in both OTA slots. |
| H13: Final integration and release disposition | All original done gates, R10-12 | Full pinned verification and exact-final-code-commit CI; current task evidence/counts reconciled. Release notes list remaining gates. Tag/publish only after the owner explicitly accepts unresolved checks or they pass; preparation alone is not publication. |

Run independent software work before waiting for physical help. When equipment
or owner action is missing, name the exact H gate, prerequisite and unverified
portion; continue other tasks. This plan does not presume that previously
connected hardware or credentials are currently available.

## 14. Integration and completion

Run focused regressions as changes are made, then `./tools/verify.sh`; firmware
changes additionally require the activated pinned environment and
`./tools/verify.sh --firmware`. Include simulator contracts, headless display,
Python tools, generated contracts, formatting, lint and production web build.
Record exact-commit CI separately from local checks. Confirm firmware fits both
OTA slots and bulk history/JSON work does not regress internal RAM headroom.

The implementation milestone requires F1–F9 fixes, regressions and reconciled
documents. Full acceptance additionally requires H01–H13 or explicit documented
owner decisions on the affected requirements. A waiver records residual risk and
scope; it is not evidence that an unperformed test passed. Keep those two
milestones distinct in progress reports and handoffs.
