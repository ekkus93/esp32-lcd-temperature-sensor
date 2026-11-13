# Review Audit Fixes — TODO

**Specification:** [REVIEW_AUDIT_FIX_SPEC.md](REVIEW_AUDIT_FIX_SPEC.md)
**Created:** 2026-11-12, following review of baseline `b20a4f9`.
**State:** planning only; every task starts open. Existing implementation and
prior validation are inputs, not proof that these regressions or gates pass.

## Working agreement

- Follow the spec's compatibility, fault-injection and evidence rules (§2).
- Reproduce each F finding in a focused regression before fixing it. Test the
  production integration boundary responsible for the defect.
- Check a parent only when every required subtask and its stated acceptance
  pass. Link evidence and the implementation SHA. Keep physical checks separate.
- Use new IDs below when annotating related `REVIEW_FIX_TODO.md` tasks. Do not
  close an old hardware gate merely because the corresponding software fix lands.
- Work through software tasks while equipment-dependent checks are blocked.
  Keep credentials private; no erase-flash, partition changes or production-data
  corruption. Release publication is a separate owner decision.

## Phase A — Test seams and P1 fixes

- [x] **A00 — Establish reproducible integration tests** (spec §§2, 14).
  - [x] **A00.1** Record starting SHA/tree, configuration and focused baseline
    results; preserve the full-suite Settings failure as a regression scenario.
    Done: `docs/validation/review-audit-baseline-2026-11-12.md` (the Settings scenario is reproduced
    deliberately in A08).
  - [x] **A00.2** Add the smallest necessary production-path seams for storage
    outcomes/deadlines, save application and HTTP JSON/stream construction.
    Keep doubles at I/O boundaries rather than copy implementations into tests.
    Done: seams `export_history_csv`/`visit_history_range` (A02), `SensorSettingsPersistence`
    (A03), `build_history_json` (A05).
  - [x] **A00.3** Assert host fault fixtures use temporary stores and cannot
    alter device history, auth or Wi-Fi. Document what remains device-only.
    Done: `tools/tests/test_host_fixtures_isolated.py`; device-only list in the baseline record.

- [x] **A01 — F1: Repair calibration hold lifecycle** (spec §4; R11-01/02/04).
  - [x] **A01.1** Reproduce loss of renewal after a steady result and after
    give-up followed by retry, advancing beyond the original hold deadline.
  - [x] **A01.2** Separate measurement timers from the single flow-level
    five-minute renewal schedule; keep the 15-minute request and 20-minute cap.
  - [x] **A01.3** Order initial/renewal/tail requests across Done, Cancel,
    Escape and unmount; prevent late installation and post-close extension.
  - [x] **A01.4** Handle failed renewal/tail, faults and revision stops with
    honest actionable UI; re-establish protection before measurement retry.
  - [x] **A01.5** Pass fake-time lifecycle, pending-request/exit and existing
    arithmetic/ETag/unit tests. Record simulator long-open behavior; leave H10 open.
    Done: `holdKeeper.ts`, dialog changes and tests; record
    `docs/validation/review-audit-hold-lifecycle-2026-11-12.md`. H10 (A19) stays open.

- [x] **A02 — F2: Make CSV completion honest** (spec §5; R02-05/09, R04-07).
  - [x] **A02.1** Reproduce successful HTTP termination despite a skipped
    completed segment through the production export path.
  - [x] **A02.2** Propagate structured complete/skipped/error outcomes from
    store to service/exporter without losing the skip count or failure cause.
  - [x] **A02.3** Return 503 before streaming, or unsuccessful termination
    after streaming; preserve CSV schema and never emit a normal final chunk
    for incomplete data. Document partial-file limitations.
  - [x] **A02.4** Test first/middle/last completed corruption, active corruption,
    healthy/empty results and send failure; verify JSON stays valid/truncated.
    Done: `history_visit.{hpp,cpp}`, `export_history_csv()`, test `history_export_incomplete`;
    record `docs/validation/review-audit-csv-completion-2026-11-12.md`.

## Phase B — Firmware integration and bounded resources

- [x] **A03 — F3: Retry sensor settings after transient storage errors**
  (spec §6; R04-08).
  - [x] **A03.1** Reproduce the main/API permanent lockout after open, set and
    commit failures, even though the lower-layer service remains writable.
  - [x] **A03.2** Keep loaded corrupt storage read-only; let a subsequent edit
    retry after transient failure. Restore committed state/revision and clear
    pending state without false success or breaking If-Match behavior.
  - [x] **A03.3** Cover both user-update and discovery/calibration persistence
    paths; bound/back off any automatic retries and keep acquisition operating.
  - [x] **A03.4** Prove second-edit success without reboot, durable contents,
    truthful writable/applied status and retained corrupt-load protection.
    Done: `SensorSettingsPersistence`, main loop and Sensors page; v1 kept (timed `read_only`
    pause); record `docs/validation/review-audit-settings-retry-2026-11-12.md`.

- [x] **A04 — F4: Cancel expired history work cooperatively**
  (spec §7; R02-05, R07-04, R10-03). Coordinate outcomes with A02.
  - [x] **A04.1** Reproduce continued storage reads/validation after the real
    service deadline, including a path that delivers no records.
  - [x] **A04.2** Distinguish ordinary delivery stop/pagination from deadline
    cancellation. Add checks per segment, record and at most 4 KiB read/hash chunk.
  - [x] **A04.3** Stop starting further work on expiry; preserve integrity
    checks for completed visits and reuse validated data without unbounded memory.
  - [x] **A04.4** Preserve three-/fifteen-second service budgets and explicit
    incomplete outcomes; release mutexes, worker jobs and limiter slots on all exits.
  - [x] **A04.5** Test hash/validation expiry, corruption, pagination,
    out-of-range indexing, disconnected send and writer contention with fake time.
  - [x] **A04.6** Document budget start, mutex wait and non-preemptible I/O
    residuals; record host performance. Keep actual full-store latency under H07
    open until measured, or explicitly revise the objective with owner approval.
    Done: `HistoryVisitControl`, test `history_cancel_deadline`; record
    `docs/validation/review-audit-history-cancel-2026-11-12.md`. H07 latency stays open.

- [x] **A05 — F7: Make history JSON allocation-safe** (spec §10; R05-03).
  - [x] **A05.1** Reproduce missing required `gaps` on a zero-gap allocation
    failure using the production response builder.
  - [x] **A05.2** Check root, scalars and both arrays; provide a bounded
    failure response when the envelope or serialization cannot be built.
  - [x] **A05.3** Permit only complete attached rows and a successfully set
    `truncated: true` for partial 200 responses; otherwise fail explicitly.
  - [x] **A05.4** Fault-inject envelope, point/gap rows, truncation and
    serialization; schema-validate all 200 bodies and check cleanup/admission reuse.
    Done: `history_json.{hpp,cpp}`, test `history_json_allocation_faults`; record
    `docs/validation/review-audit-history-json-2026-11-13.md`.

## Phase C — Web correctness and reliable verification

- [x] **A06 — F5: Adapt chart tick spacing** (spec §8; R12-05).
  - [x] **A06.1** Reproduce the Los Angeles 13:00–14:00 empty-tick window.
  - [x] **A06.2** Implement span/density-aware intervals and endpoint fallback;
    keep finite ordered in-range ticks, useful uptime labels and bounded work.
  - [x] **A06.3** Test zooms, one-hour/day/week/month ranges, fractional zones,
    DST folds/gaps and invalid windows. Keep repeated local times distinguishable.
  - [x] **A06.4** Inspect narrow/wide browser charts and phone label thinning;
    record screenshots without claiming physical phone acceptance.
    Done: `scale.ts` and phone thinning; record
    `docs/validation/review-audit-chart-ticks-2026-11-13.md` (simulator shows uptime axes only).

- [x] **A07 — F6: Validate dashboard history identity** (spec §9; R12-04).
  - [x] **A07.1** Reproduce reboot/overlapping-uptime and first-sync failures
    that reuse incompatible cached data against a new axis.
  - [x] **A07.2** Bind accepted results to request identity and bounds; clear
    incompatible data and reject late obsolete responses.
  - [x] **A07.3** Show stale/error status for retained same-context data with
    its original bounds, independently of live-snapshot freshness.
  - [x] **A07.4** Test range changes, failed refreshes and recovery; assert
    plotted values/axis and one query per range/five-minute refresh.
    Done: `Dashboard.tsx` history entry with request context and bounds; record
    `docs/validation/review-audit-dashboard-history-2026-11-13.md`.

- [x] **A08 — F8: Stabilize Settings save tests** (spec §11; R06-08, R12-06).
  - [x] **A08.1** Deliberately interleave `/clock` polling with a settings save
    to expose the existing last-fetch assumption deterministically.
  - [x] **A08.2** Route mocks by URL/method and assert the PATCH request,
    precondition/payload and applied callback; restore timers/mocks after each test.
  - [x] **A08.3** Pass focused and full web suites, including controlled poll
    interleaving, without dropping existing behavioral coverage.
    Done: routed mocks and forced clock interleaving; record
    `docs/validation/review-audit-settings-test-2026-11-13.md`.

## Phase D — Documentation and acceptance integrity

- [x] **A09 — F9: Reconcile current documentation**
  (spec §12; R09-08, R10-11/13). Finalize after the behavior fixes.
  - [x] **A09.1** Correct public CSV guidance in `HISTORY_FORMAT.md`,
    `DEVELOPMENT.md` and any other current contradictory instructions.
  - [x] **A09.2** Describe actual completion/deadline/performance behavior;
    keep API/storage version decisions explicit and distinguish measured results
    from forecasts and unperformed acceptance.
  - [x] **A09.3** Recount numbered original tasks separately from gates and
    update every current summary. Preserve dated historical records as history.
  - [x] **A09.4** Cross-check specs, contracts, guides, release notes and
    trackers for removed owner code, four-client cap, fonts/access and evidence.
  - [x] **A09.5** Annotate affected original tasks with fix IDs and evidence;
    retain incomplete subrequirements/hardware gates rather than blanket closure.
    Done: record `docs/validation/review-audit-docs-2026-11-13.md` (`PROGRESS.md` left as dated
    history).

- [ ] **A10 — H04: Complete safe route-boundary coverage** (spec §13; R07-06).
  - [x] **A10.1** Inventory registered routes against the single access table.
  - [ ] **A10.2** Exercise anonymous, session and missing-CSRF refusal through
    production dispatch, including destructive handlers with side effects mocked.
  - [x] **A10.3** Assert no forbidden side effect is reached; record actual
    handler coverage separately from policy-table and physical board evidence.
    A10.1/A10.3 done: production gate, `test_route_inventory.py`; record
    `docs/validation/review-audit-route-boundary-2026-11-13.md`. A10.2 open: handlers are not
    run on the host (needs a production-dispatch harness); gate and order are covered.

- [x] **A11 — H09: Finish calibration API/reader compatibility evidence**
  (spec §13; R11-01).
  - [x] **A11.1** Cover connected/disconnected 409 behavior and accepted/refused
    minute values at 1/20 and outside the range, including malformed values.
  - [x] **A11.2** Prove an actual older reader skips new calibration event kinds
    and reads later known events using an isolated/copied journal fixture.
  - [x] **A11.3** Record connected-board cases separately; retain firmware
    cap/expiry, route access, events/history exclusion and CAL display regressions.
    Done: `calibration_hold_request`, older-reader tests under `firmware/tests/compat/`; record
    `docs/validation/review-audit-calibration-compat-2026-11-13.md`. No board cases (no probes).

- [x] **A12 — H11: Complete the UI quality review** (spec §13; R12-07).
  - [x] **A12.1** Review loading, empty and every reachable error state for an
    actionable next step; record/fix any newly confirmed defect with a regression.
  - [x] **A12.2** Verify keyboard focus, accessible names, contrast and reduced
    motion in both themes; state limits of automated accessibility checks.
  - [x] **A12.3** Inspect all six page comparisons at 390/1280 px; capture changed
    views and verify overflow at 360/390/768/1280/1600 px. Record the review.
    Done: switch focus ring fixed (`theme.ts`, regression in `tokens.test.ts`); review
    `docs/validation/ui-quality-2026-11-13/`. Phone acceptance stays with H12.

## Phase E — Physical and operational evidence

These tasks require equipment or runtime evidence. Run independent software
work first; do not infer completion from simulator tests. Observe the wiring
hold points and preserve device data during experiments.

- [ ] **A13 — H01: Calibrated logging and bus recovery** (R01-07).
  - [ ] **A13.1** Log a calibrated probe for at least 30 minutes; account for
    every expected complete bucket and document boundary/in-progress windows.
  - [ ] **A13.2** With multiple probes and one above its limit, disconnect and
    restore the bus; verify one bus fault, no alarm storm and correct recovery.

- [ ] **A14 — H02: Recovery and two-client authentication load** (R02-01/08).
  - [ ] **A14.1** Arrange an isolated/preserved auth-recovery test; prove USB
    recovery with corrupt auth while Wi-Fi/history remain intact.
  - [ ] **A14.2** Use two physical client devices for wrong-login load,
    uninterrupted live/dashboard traffic and owner sign-in from the second client.

- [ ] **A15 — H03: Observe physical LCD behavior** (R03-05/08, R08-06).
  - [ ] **A15.1** Measure setup display appearance against 200 ms; distinguish
    physical/optical timing from scheduling or serial-log timing.
  - [ ] **A15.2** Watch wrong-Wi-Fi setup/readings/alerts and AP expiry; inspect
    the active local name after a simulated mDNS conflict.

- [ ] **A16 — H05/H06: Phone access and client-platform mDNS** (R07-08, R08-05/06).
  - [ ] **A16.1** On a real anonymous phone, verify live/history/alerts and
    sign-in for edits; load history repeatedly while tracking dashboard cadence.
  - [ ] **A16.2** Record default-name resolution, rename and conflict behavior
    on Linux, Windows, macOS/iOS and Android, with resolver details/IP fallback.
    Leave unavailable platform checks open pending explicit disposition.

- [ ] **A17 — H07: Retention, rollover and history performance** (R10-03/08).
  - [x] **A17.1** Produce one-to-five-probe full-budget host stores; record
    actual encoded sizes, retained timestamps and eviction correctness.
    Done (host only): `docs/validation/review-audit-retention-host-2026-11-13.md`.
  - [ ] **A17.2** Measure device oldest/newest data and rollover when feasible;
    distinguish device measurements, accelerated fixtures and capacity forecasts.
  - [ ] **A17.3** Profile short/full-window queries and CSV after A02/A04 with
    concurrent status/live/writer traffic; record latency samples, deadline
    residuals, gaps and RAM, and disposition the original under-one-second goal.

- [ ] **A18 — H08: Instrument and run the long soak** (R10-10).
  - [ ] **A18.1** Add necessary diagnostics for LVGL pool/high-water marks,
    flash-write counts and Wi-Fi traffic alongside existing RAM/client/display data.
  - [ ] **A18.2** Run longer than 24 hours and observe cache fill/plateau;
    report drift, high-water marks, faults and expected bounded growth separately.

- [ ] **A19 — H10: Validate real ice-bath calibration** (R11-04).
  - [ ] **A19.1** After A01/A11 pass, use a real probe and bath to verify
    0.0 ± 0.1 °C and stored offset persistence after restart.
  - [ ] **A19.2** Observe extended-open hold renewal, no bath-induced low
    alarm, history gap, CAL, start/end events and correct tail expiry/recovery.

- [ ] **A20 — H12: Verify device-hosted offline/phone UI** (R12-08).
  - [ ] **A20.1** Remove internet availability while retaining device access;
    load all pages, local font/licence and inspect real phone bottom navigation.
  - [x] **A20.2** Record packed web size and firmware fit in both OTA slots;
    retain existing size budgets and distinguish cached from fresh asset loads.
    Done (build evidence): `docs/validation/review-audit-web-size-2026-11-13.md`; cached/fresh
    loads left to A20.1 on a phone.

## Phase F — Integration and disposition

- [x] **A21 — H13: Verify and reconcile the final implementation** (spec §14).
  - [x] **A21.1** Run focused regressions and `./tools/verify.sh`; with pinned
    ESP-IDF activated, pass `./tools/verify.sh --firmware` for firmware changes.
  - [x] **A21.2** Record exact-final-code-SHA CI, contract freshness, artifact
    provenance, slot size and relevant memory/performance results.
  - [x] **A21.3** Reconcile new/old task status and all five original done
    gates; list every remaining H gate, missing prerequisite and owner decision.
    A local pass or old CI record does not replace exact-commit evidence.
    Done: code `3c52431`, CI run 43399938296 passed; record
    `docs/validation/review-audit-final-2026-11-13.md` (remaining gates and owner decisions).

- [ ] **A22 — H13: Record release disposition** (R10-12).
  - [x] **A22.1** Update release notes with fixed findings and residual
    acceptance gaps; keep software completion distinct from full acceptance.
    Done: `docs/RELEASE_NOTES_0.2.0.md` gains the audit fixes and new known issues.
  - [ ] **A22.2** Obtain the owner's explicit decision for any unresolved
    requirement before release; document waivers as waivers, not passing tests.
  - [ ] **A22.3** Only if release publication is explicitly authorized, tag and
    publish validated firmware/web provenance, checksums and notes from the exact
    verified revision. Otherwise leave publication pending with its reason.
    Pending: A22.2 needs the owner's decisions (listed in `review-audit-final-2026-11-13.md`);
    publication is not authorized, so nothing is tagged or published.

## Completion gates

- [ ] **G1 — Software remediation:** A00–A12 satisfy F1–F9 and safe test/quality
  coverage; all required regressions pass and current guidance agrees with code.
- [ ] **G2 — Physical acceptance:** A13–A20 have recorded evidence for each
  required gate, or explicit owner-approved scope decisions with residuals named.
- [x] **G3 — Final validation:** A21 is complete; no unreported failed checks,
  stale exact-commit claims or tracker/count discrepancies remain.
- [ ] **G4 — Release:** A22's disposition is recorded; actual publication is
  claimed only when authorized and successfully completed.
