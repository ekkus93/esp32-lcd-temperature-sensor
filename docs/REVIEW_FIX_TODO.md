# Review Fixes and Access Changes — TODO

**Spec:** [`REVIEW_FIX_SPEC.md`](REVIEW_FIX_SPEC.md) (section numbers below refer to it)
**Created:** 2026-11-05 from the full code review and two owner requests (public read access, dependable mDNS).

## Working agreement

- Check a task only when its acceptance criteria pass, with evidence. `[HW]` tasks need the connected board; record them under `docs/validation/` and keep host, simulated and physical evidence separate.
- For a defect, write the failing test first, then fix. Run the focused test, then `./tools/verify.sh`; firmware tasks also `./tools/verify.sh --firmware`.
- One task per commit. No `erase-flash`, no partition change. API, schema and storage changes record their version decision.
- Open decisions D1-D7 (spec §14) are settled before the tasks that depend on them.

## Phase R0 — Decisions

- [ ] **R00-01** Owner settles D1-D7 (spec §14); record the answers in the spec.

## Phase R1 — Data correctness (P0)

- [x] **R01-01** Clamp the bucket average to `[minimum, maximum]` (§3.1). Done: the new `aggregator_calibration` host test failed before the fix and passes after it; `./tools/verify.sh` green (26 host tests, display test, 78 web tests). Tests: calibrated steady and varying readings encode and round-trip; out-of-range data still rejected.
- [x] **R01-02** Bus fault preserves alarm state; recovery returns straight to `high`/`low`/`disconnected` without a new persistence wait or duplicate event (§3.2). Done: the new `bus_fault_alarm` host test failed before the fix and passes after it (still-high, hysteresis, cleared, normal-then-over-limit, disconnected, bus-event count, disabled); `./tools/verify.sh` green (27 host tests).
- [x] **R01-03** Move 1-Wire scan decisions into a host-testable `monitor_core` policy (§4.4); `OneWireService` keeps only ESP-IDF calls. Done: new `ScanPolicy` (`monitor_core/scan_policy.*`) with characterization tests of the previous behaviour (`scan_policy` host test); `OneWireService` now only makes driver calls; firmware builds; `./tools/verify.sh` green (28 host tests). Behaviour is unchanged; R01-04 and R01-05 change it.
- [x] **R01-04** Suspect scans: a smaller scan is accepted only after two agree; previous ROMs keep being read meanwhile (§3.3). Done in `ScanPolicy`: suspect scan keeps the old read list, reports the scan incomplete (so nothing can be retired on it) and rescans in 5 s; tests cover mid-search drop, two agreeing scans, glitch then full scan, differing smaller scans, growth and replacement. `./tools/verify.sh` green; firmware builds.
- [x] **R01-05** No presence pulse with sensors expected is one bus fault, not N disconnects (§3.3). Done in `ScanPolicy`: with two or more probes being read, an empty scan is a bus fault (read list kept, rechecked every 5 s, no flicker, cleared when probes or reads return); five minutes of empty scans are believed and the sensors become `disconnected`; a single probe unplugged stays `disconnected` after a second scan. End-to-end host test with the registry: all probes vanishing gives one bus-level event and no per-sensor events. `./tools/verify.sh` green; firmware builds.
- [x] **R01-06** Enable `CONFIG_MONITOR_SENSOR_BUS` in `sdkconfig.defaults`; add a bare-board defaults file; "bus disabled" is a distinct state with no disconnect alarms; RMT init failure is a bus fault (§4.1). CI checks the resolved option. Done: `sdkconfig.defaults` enables the bus (a clean configure resolves to `=y`; `sdkconfig.bareboard` overlay resolves to off); a build with the bus compiled out shows saved probes as `disabled` (OFF) with no events or alarms (host tests in `bus_fault_alarm`); a driver init failure sets the bus fault (ESP-only code: verified by build and review, no host test); CI step greps the resolved `firmware/sdkconfig`; `DEVELOPMENT.md` updated. `./tools/verify.sh` green; firmware builds.
- [ ] **R01-07** [HW] Flash the fixed firmware; one calibrated probe for 30 minutes stores every bucket; unplug the probe bus briefly and confirm one bus-fault event and correct recovery of a probe held above its limit.

## Phase R2 — Recovery and stalls (P1)

- [ ] **R02-01** USB recovery console and BOOT service start even when auth failed to initialise; console reset recovers a corrupt auth store without touching history or Wi-Fi (§6.1). Host tests for the auth-load policy.
- [ ] **R02-02** OTA health-gate timeout: a pending image that hasn't passed in 120 s marks itself invalid and reboots; enable task-watchdog panic (§6.2). Host test with a fake clock.
- [ ] **R02-03** Password derive off the HTTP task; one derive at a time with `429` for overlaps; per-client lockout with a global cap; KDF failures don't count as wrong passwords (§6.3). Host tests for the lockout policy.
- [ ] **R02-04** Event store recovers a leftover `events.bin.tmp`; change `storage_tests.cpp:940-966` to require recovery (§7.1).
- [ ] **R02-05** History query: single validation pass, per-segment time index, 3 s deadline with `truncated`, mutex released between segments; CSV export completes at full retention or says it didn't (§7.2). Host profile records reads per query.
- [ ] **R02-06** Web heartbeat closes only open sockets; `connect()` resets the silence timer; no banner flicker; test fires `onopen`, silence and reconnect (§8.1).
- [ ] **R02-07** [HW] OTA-inject a failing HTTP service: the image rolls back by itself within 2.5 minutes, no physical reset.
- [ ] **R02-08** [HW] Repeated wrong logins from one machine while another machine streams the dashboard: the dashboard and WebSocket keep updating and the owner can still sign in from the second machine.
- [ ] **R02-09** [HW] Full-retention (or largest available) history query and CSV export on the device: latency recorded, other requests keep responding.

## Phase R3 — Setup mode, LCD and owner code (P1/P2)

- [ ] **R03-01** Every AP opening has a deadline; a failed candidate closes the AP after 10 minutes once the station is back; unprovisioned devices keep the AP (§5.1). Host tests of `WifiApWindow`.
- [ ] **R03-02** While the AP is open, the LCD alternates setup and Overview every 10 s; alerts stay visible (§5.1, D6). Display test.
- [ ] **R03-03** Compact footer shows `Log ERR` with priority over the Wi-Fi state (§5.2). Display test.
- [ ] **R03-04** `Hold BOOT NN%` takes priority over `BUS FAULT` (§5.3). Display test.
- [ ] **R03-05** Setup screen appears within 200 ms of the hold completing (§5.4).
- [ ] **R03-06** Owner code normalised on the device (spaces, hyphens, line breaks, case); invalid characters rejected without lockout; web field visible with a hint (§5.5, D7). Host and web tests.
- [ ] **R03-07** Display cleanups: comment fix, blank screen before destroying the view, flush-diagnostic callback check, single "compact" decision, tests use named accessors, cheaper frame hand-off with a dropped-frame count, no "Past N min" on the large Overview (§5.6).
- [ ] **R03-08** [HW] Submit a wrong Wi-Fi network from Settings: Overview and alerts return on the LCD, and the AP closes after the window. Owner code typed in lower case with a space is accepted on an unenrolled device.

## Phase R4 — Sensor details and events (P2)

- [ ] **R04-01** 85.0 °C accepted only when consistent with the previous reading or confirmed by the next (§3.4). Replace the flag-only test.
- [ ] **R04-02** A ROM CRC error during the search rescans; a bus fault only if it repeats (§3.5). Host test.
- [ ] **R04-03** Scan retry backoff 5/10/20 s capped at 60 s, reset on a clean scan (§3.6). Host test.
- [ ] **R04-04** One function owns per-sensor state changes; identical repeated transitions limited to one per 60 s in the journal; decide `State::unassigned` (§3.7). Update `alert_service_tests.cpp:150-160`.
- [ ] **R04-05** `storage_error` events limited to state changes plus one per hour (§7.3). Host test.
- [ ] **R04-06** `event_healthy_` recovers after successful writes; `storage_info()` keeps last values on mutex timeout (§7.4).
- [ ] **R04-07** Skip and report one corrupt completed history segment; retry transient append errors (§7.5). Host tests.
- [ ] **R04-08** Duplicate display positions, `excess_`/overflow, NVS commit retry, stale/invalid flapping in the display snapshot (§4.5).

## Phase R5 — API and contract (P2)

- [ ] **R05-01** `PATCH /settings` and `PATCH /sensors/{rom}` reject `{}` (§7.6). API test.
- [ ] **R05-02** History query ROM rules (lowercase, no duplicates) enforced or documented (§7.6). Host test.
- [ ] **R05-03** cJSON allocation failure sets `truncated`; receive override removed on stop; mutex freed on start failure (§7.6).
- [ ] **R05-04** WebSocket backpressure documented or a one-frame slot added; T07-08 wording corrected (§7.6).
- [ ] **R05-05** OTA upload checks `project_name` before writing; docs say signing and secure boot are out of scope (§6.5).

## Phase R6 — Web application (P2/P3)

- [ ] **R06-01** Failed resync keeps live values shown as stale (§8.2).
- [ ] **R06-02** `request()` checks status before parsing; readable errors; 401 still opens sign-in (§8.2).
- [ ] **R06-03** Empty display position gives a field error (§8.2).
- [ ] **R06-04** `temperature()` unit argument required; failed `getSettings` shows an error and retries (§8.2).
- [ ] **R06-05** History: readable gap tooltips, empty zoomed view message, re-fetch after first clock sync (§8.2).
- [ ] **R06-06** Thresholds and calibration in the configured unit or clearly labelled °C (§8.2, D5).
- [ ] **R06-07** Accessibility: `aria-pressed` on range chips, `role="alert"` on errors, saved theme (§8.2).
- [ ] **R06-08** Tests: URL-routed fetch in `App.test.tsx` with a saved `units: 'C'` case; fake socket fires `onopen`; App tests stub `WebSocket`; simulator defaults (`units: 'F'`, `probe-scan enabled:false`) and non-empty history and alerts validated by `test-simulator-live.mjs` (§8.3).

## Phase R7 — Public read-only access (feature, §12)

- [ ] **R07-01** Route policy table from §12.1 implemented as one access function in `http_api.cpp`, with host-testable rules (D1-D3).
- [ ] **R07-02** Public `GET /settings` response without owner-only fields; `auth/state` reports `signed_in`; contracts, `API.md` and generated types updated with the version decision (§12.2).
- [ ] **R07-03** Public WebSocket upgrade (Origin still checked) with the 8-client cap and a reserved owner slot (§12.1, §12.4).
- [ ] **R07-04** Per-client rate limits: history 1/s, export 1 per 30 s, 2 concurrent history queries, `429` with `Retry-After` (§12.4). Host tests.
- [ ] **R07-05** Web: Dashboard, History and Alerts without sign-in; Sensors and Settings show an inline sign-in and owner forms after it; sign-in/out control in the app bar; 401 opens sign-in (§12.3). Web tests.
- [ ] **R07-06** API tests: every route anonymous and signed-in; mutations without session or CSRF rejected (§12.5).
- [ ] **R07-07** Update `SECURITY_DESIGN.md` and `USER_GUIDE.md` for the public model (§9, §10).
- [ ] **R07-08** [HW] A phone with no session sees live readings, history and alerts; changing a threshold asks for the password; repeated public history requests don't stall the dashboard.

## Phase R8 — mDNS (feature, §13)

- [ ] **R08-01** `hostname` device setting with RFC 1123 validation; settings record `TMD2` with `TMD1` migration; schemas and version decision updated (§13.1, §13.2). Host tests.
- [ ] **R08-02** Rename re-registers mDNS without reboot and updates the Host allow-list; active name read back after conflicts (§13.1, D4).
- [ ] **R08-03** Advertise `_http._tcp` with TXT `path=/` and `model=temperature-monitor`; `GET /status` includes `mdns_hostname`; mDNS failure reported, IP access unaffected (§13.1).
- [ ] **R08-04** Show `http://<name>.local` in web Settings and on the LCD setup screen once the station is connected (§13.1). Display and web tests.
- [ ] **R08-05** [HW] Resolve `http://temperature-monitor.local` from Linux, Windows, macOS/iOS and Android; rename and re-resolve without reboot; note clients that can't resolve `.local`.
- [ ] **R08-06** [HW] Two devices on one network (or a simulated conflicting responder) end up with distinct, displayed names.

## Phase R9 — Build, docs and release (P3)

- [ ] **R09-01** Remove the host test suite from the firmware image; keep non-fatal on-device self-tests reported in status (§4.2).
- [ ] **R09-02** Remove or use the `espressif/ds18b20` dependency (§4.3).
- [ ] **R09-03** `verify.sh` discovers Python tests; CI runs the leak checker and checks generated contract types are current (§4.3).
- [ ] **R09-04** Auth internals: dedicated notification index, safe cross-task read of the setup secret and enrolled flag, `AuthService` host tests behind an NVS/KDF seam (§6.4).
- [ ] **R09-05** Release provenance: compare `web/dist` with `web_assets.json`, ESP-IDF commit from the SDK, `--commit` equals `HEAD`, refuse a dirty tree, version from the app descriptor, build the web once in CI (§9).
- [ ] **R09-06** Leak checker scans staged files and recent history for current setup values and fails instead of skipping (§9).
- [ ] **R09-07** Update `SECURITY_DESIGN.md` for the 80-bit owner code and 60-bit Wi-Fi key; `USER_GUIDE.md` for °F default, both units on the LCD and the `.local` address (§9).
- [ ] **R09-08** With owner approval, correct `docs/TODO.md`: reopen or annotate the affected checked tasks, move T09-14's notes, refresh T11-09's counts (§9).
- [ ] **R09-09** Design cleanups from §11 (USB bridge naming, host-only identity checks documented or simplified).

## Definition of done

- [ ] All P0 and P1 items pass with tests, and their `[HW]` checks are recorded.
- [ ] Anonymous users can read readings, history and alerts; every change needs the owner password; rate limits hold under load.
- [ ] The device is reachable at its `.local` name from the owner's usual devices, and the active name is shown on the web and LCD.
- [ ] `./tools/verify.sh --firmware` passes and CI is green on the final commit.
- [ ] `docs/TODO.md` matches the code after R09-08.
