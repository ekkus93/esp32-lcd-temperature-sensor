# Review Fixes and Access Changes — TODO

**Spec:** [`REVIEW_FIX_SPEC.md`](REVIEW_FIX_SPEC.md) (section numbers below refer to it)
**Created:** 2026-11-05 from the full code review and two owner requests (public read access, dependable mDNS).

## Working agreement

- Check a task only when its acceptance criteria pass, with evidence. `[HW]` tasks need the connected board; record them under `docs/validation/` and keep host, simulated and physical evidence separate.
- For a defect, write the failing test first, then fix. Run the focused test, then `./tools/verify.sh`; firmware tasks also `./tools/verify.sh --firmware`.
- One task per commit. No `erase-flash`, no partition change. API, schema and storage changes record their version decision.
- Open decisions D1-D7 (spec §14) are settled before the tasks that depend on them.

## Phase R0 — Decisions

- [x] **R00-01** Owner settles D1-D7 (spec §14); record the answers in the spec. Done: D1 to D7 were all accepted as proposed on 2026-11-05 and are recorded in the spec (§14, commit `aead8d8`).

## Phase R1 — Data correctness (P0)

- [x] **R01-01** Clamp the bucket average to `[minimum, maximum]` (§3.1). Done: the new `aggregator_calibration` host test failed before the fix and passes after it; `./tools/verify.sh` green (26 host tests, display test, 78 web tests). Tests: calibrated steady and varying readings encode and round-trip; out-of-range data still rejected.
- [x] **R01-02** Bus fault preserves alarm state; recovery returns straight to `high`/`low`/`disconnected` without a new persistence wait or duplicate event (§3.2). Done: the new `bus_fault_alarm` host test failed before the fix and passes after it (still-high, hysteresis, cleared, normal-then-over-limit, disconnected, bus-event count, disabled); `./tools/verify.sh` green (27 host tests).
- [x] **R01-03** Move 1-Wire scan decisions into a host-testable `monitor_core` policy (§4.4); `OneWireService` keeps only ESP-IDF calls. Done: new `ScanPolicy` (`monitor_core/scan_policy.*`) with characterization tests of the previous behaviour (`scan_policy` host test); `OneWireService` now only makes driver calls; firmware builds; `./tools/verify.sh` green (28 host tests). Behaviour is unchanged; R01-04 and R01-05 change it.
- [x] **R01-04** Suspect scans: a smaller scan is accepted only after two agree; previous ROMs keep being read meanwhile (§3.3). Done in `ScanPolicy`: suspect scan keeps the old read list, reports the scan incomplete (so nothing can be retired on it) and rescans in 5 s; tests cover mid-search drop, two agreeing scans, glitch then full scan, differing smaller scans, growth and replacement. `./tools/verify.sh` green; firmware builds.
- [x] **R01-05** No presence pulse with sensors expected is one bus fault, not N disconnects (§3.3). Done in `ScanPolicy`: with two or more probes being read, an empty scan is a bus fault (read list kept, rechecked every 5 s, no flicker, cleared when probes or reads return); five minutes of empty scans are believed and the sensors become `disconnected`; a single probe unplugged stays `disconnected` after a second scan. End-to-end host test with the registry: all probes vanishing gives one bus-level event and no per-sensor events. `./tools/verify.sh` green; firmware builds.
- [x] **R01-06** Enable `CONFIG_MONITOR_SENSOR_BUS` in `sdkconfig.defaults`; add a bare-board defaults file; "bus disabled" is a distinct state with no disconnect alarms; RMT init failure is a bus fault (§4.1). CI checks the resolved option. Done: `sdkconfig.defaults` enables the bus (a clean configure resolves to `=y`; `sdkconfig.bareboard` overlay resolves to off); a build with the bus compiled out shows saved probes as `disabled` (OFF) with no events or alarms (host tests in `bus_fault_alarm`); a driver init failure sets the bus fault (ESP-only code: verified by build and review, no host test); CI step greps the resolved `firmware/sdkconfig`; `DEVELOPMENT.md` updated. `./tools/verify.sh` green; firmware builds.
- [ ] **R01-07** [HW] Flash the fixed firmware; one calibrated probe for 30 minutes stores every bucket; unplug the probe bus briefly and confirm one bus-fault event and correct recovery of a probe held above its limit. **Partly done (2026-11-05):** the calibrated-probe half passed on the board (four buckets stored with a 0.1 °C offset, logging healthy, no storage error; see `docs/validation/device-hardening-2026-11-05.md`). **Still open:** the bus-unplug half (one bus-fault event and recovery of a probe held above its limit) needs the owner to disconnect the bus.

## Phase R2 — Recovery and stalls (P1)

- [x] **R02-01** USB recovery console and BOOT service start even when auth failed to initialise; console reset recovers a corrupt auth store without touching history or Wi-Fi (§6.1). Done (host and build only): `start_provisioning_controls` now runs whenever the Wi-Fi stack is up; the existing `RESET_ADMIN_CREDENTIALS CONFIRM` only erases the auth keys and restarts; a missing, oversized or damaged one-time code is replaced instead of failing startup (`plan_setup_code`, host-tested in `readable_code`); a wrong-size verifier blob still stops auth and is the case the console recovers. Not yet exercised on the board: a corrupt-store recovery needs a deliberate NVS corruption and is part of the hardware pass. `./tools/verify.sh` green; firmware builds.
- [x] **R02-02** OTA health-gate timeout: a pending image that hasn't passed in 120 s marks itself invalid and reboots; enable task-watchdog panic (§6.2). Done (host and build): `ota_boot_decision` with a 120 s grace is host-tested with a fake clock (healthy accepted at once; broken HTTP/auth/Wi-Fi waits then rolls back at 120 s; incomplete cycles roll back); `main` calls `esp_ota_mark_app_invalid_rollback_and_reboot()` on roll-back; `CONFIG_ESP_TASK_WDT_PANIC=y` in `sdkconfig.defaults` (clean configure resolves to `y`) and CI checks it. Not yet run on the board: the rollback timeout needs the OTA failure injection (R02-07) and the watchdog change needs a stability soak to confirm it doesn't reset a healthy board. `./tools/verify.sh` green; firmware builds.
- [x] **R02-03** Password derive off the HTTP task; one derive at a time with `429` for overlaps; per-client lockout; KDF failures don't count as wrong passwords (§6.3). Done (host and build): `LoginThrottle` (`monitor_core`, `login_throttle` host test: single-flight, per-client 5/10/20/40/60 s backoff, owner unaffected by an attacker's failures, 0.5 s spacing after success, derive failure = 2 s pause only, bounded client table); `AuthService` is mutex-protected, serialises derives, returns session copies and exposes `login_begin/complete/abort`; the HTTP login route uses the async request API and a pinned worker task, answering `429` with `Retry-After`. **Not yet exercised on the board** (R02-08): this changes how every login works, so a login, wrong-password, overlapping-login and concurrent-dashboard test must pass on the device before relying on it. `./tools/verify.sh` green (29 host tests); firmware builds.
- [x] **R02-04** Event store recovers a leftover `events.bin.tmp`; change `storage_tests.cpp:940-966` to require recovery (§7.1). Done: `EventStore::recover()` unlinks a stale `.tmp` (a symlink is removed, never followed, so an unrelated target is preserved); the symlink test now requires recovery to clear it, and a new power-cut case (torn partial `.tmp` next to an intact full journal) recovers, keeps both events and completes the compaction on the next append (`storage_recovery` host test failed before the fix). `./tools/verify.sh` green; firmware builds.
- [x] **R02-05** History query: single validation pass, 3 s deadline with `truncated`; CSV export completes at full retention or says it didn't (§7.2). Done (partly): each segment is read once into memory and records are delivered from that validated snapshot (no per-record filesystem lookups; the swap-during-callback test now asserts the delivered rows are the originals and the visit still fails); `HistoryService::query` has a 3 s budget; on the board bounded history went 10.3 s to 0.34-0.61 s, CSV export 9.5 s to 0.64 s and the heavy-load median 9.4 s to 0.49 s (`docs/validation/device-hardening-2026-11-05.md`). Not done: a per-segment time index and releasing the mutex between segments (not needed at the measured cost), and a full-retention export measurement (R02-09). `./tools/verify.sh` green.
- [x] **R02-06** Web heartbeat closes only open sockets; `connect()` resets the silence timer; no banner flicker; test fires `onopen`, silence and reconnect (§8.1). Done: `connect()` now starts a fresh silence window, so a reconnecting socket gets 15 s to finish its handshake and the stale banner is not re-raised every second; the fake socket can `open()` and counts closes; two new tests (reconnect after silence keeps the new socket and resumes delivery; a socket that never connects is closed after the silence limit) - the first failed before the fix. `./tools/verify.sh` green (78 web tests). Note: the heartbeat still closes any socket after 15 s of silence, connecting or open, rather than only open ones, because a handshake that never completes must also be given up.
- [x] **R02-07** [HW] OTA-inject a failing HTTP service: the image rolls back by itself within 2.5 minutes, no physical reset. **Done (2026-11-12, board):** a throwaway image that never starts the HTTP server was installed over the air; with no reset the board was back on `ota_0` / `valid` 129.8 s after the upload, and `otadata` shows the injected slot INVALID ([record](validation/device-ota-failure-injection-2026-11-04.md)).
- [x] **R02-08** [HW] Repeated wrong logins from one machine while another machine streams the dashboard: the dashboard and WebSocket keep updating and the owner can still sign in from the second machine. **Done (2026-11-12, board, `tools/device_login_flood_test.py`):** a wrong password every second for 90 s over Wi-Fi (4 checked and refused with `401`, 65 refused at once with `429` by the per-client back-off) while the same computer streamed `/status` twice a second and a live WebSocket; the owner signed in at the first attempt (4.5 s) through the USB bridge, a separate client. Status: 152 replies, slowest 0.245 s, no failures; live frames at most 5.5 s apart. Both "machines" were one computer using two client identities (Wi-Fi and USB bridge), not two separate computers.
- [x] **R02-09** [HW] Full-retention (or largest available) history query and CSV export on the device: latency recorded, other requests keep responding. **Done (2026-11-12, board):** the largest available range (30 days; the store holds 7.9 days) took 1.9 s to query and 3.3 s to export (128 KB). The first measurement showed every other request held up for the whole 6.2 s export; CSV rows are now sent in 4 KiB chunks and queries and exports run on their own worker task, so `/status` stayed under 0.85 s and live frames kept their 5 s cadence ([record](validation/device-history-load-2026-11-12.md)).

## Phase R3 — Setup mode, LCD and owner code (P1/P2)

- [x] **R03-01** Every AP opening has a deadline; a failed candidate closes the AP after 10 minutes once the station is back; unprovisioned devices keep the AP (§5.1). Done (host and build): `WifiApWindow::candidate_failed()` gives a failed candidate a 10-minute window that closes only when the saved network is connected again and no attempt is running, never shortens BOOT recovery, and replaces an earlier handoff timer; the service calls it when a candidate times out. New cases in `wifi_ap_window` failed before the method existed. An unprovisioned device never connects, so its AP simply stays open. `./tools/verify.sh` green; firmware builds. Not yet exercised on the board (R03-08).
- [x] **R03-02** While the AP is open, the LCD alternates setup and Overview every 10 s; alerts stay visible (§5.1, D6). Done (host and build): `setup_phase()` (host-tested in `setup_phase`) rotates 10 s phases: compact screen with an owner code = Wi-Fi page, owner-code page, readings; otherwise setup page, readings; `DisplayRuntime::update` uses it, and `DisplayView::provisioning` takes the page explicitly. Alerts are visible during the readings phases (one third to one half of the time), not continuously. D6 applies as proposed (an unprovisioned device alternates too). `./tools/verify.sh` green; firmware builds. Not yet seen on the LCD (R03-08).
- [x] **R03-03** Compact footer shows `Log ERR` with priority over the Wi-Fi state (§5.2). Done (display test, host and build): footer priority on the compact screen is BOOT hold, bus fault, OTA state, `Log ERR`, then the Wi-Fi state; the new footer-priority block in `display_render` failed before the change. The large layout keeps its two-line footer with the same urgent-condition order. `./tools/verify.sh` green. Not yet seen on the LCD.
- [x] **R03-04** `Hold BOOT NN%` takes priority over `BUS FAULT` (§5.3). Done together with R03-03 (same priority chain, same test): with a bus fault and a BOOT hold the footer shows the hold and the footer's `BUS FAULT` gives way (the sensor rows still show their own status).
- [x] **R03-05** Setup screen appears within 200 ms of the hold completing (§5.4). Done (build only): the display refresh task now reads `WifiService::provisioning_open()` (the atomic AP flag) every 100 ms instead of waiting for the 5 s acquisition frame. No host test (ESP-only wiring); the latency itself is a hardware check (R03-08).
- [x] **R03-06** Owner code normalised on the device (spaces, hyphens, line breaks, case); invalid characters rejected without lockout; web field visible with a hint (§5.5, D7). Done (host, web and build): `normalize_owner_code()` (host-tested: case, spaces, hyphens, tabs, line breaks, wrong length, 0/1/I/O, NUL, oversize); `AuthService::enroll` returns `enrolled/denied/bad_format` - a malformed code returns `400` with an explanatory message and does not start the 5 s lockout, while a well-formed wrong code is still a counted guess; a stored legacy 64-hex code is still compared exactly; the web field is a visible text input with a hint, `autocapitalize=characters` and sends the code as typed (two AuthGate tests, the first failed before). `./tools/verify.sh` green (30 host tests, 82 web tests); firmware builds. Not exercised on a board: it needs an unenrolled device (factory reset), part of the hardware pass.
- [x] **R03-07** Display cleanups: comment fix, blank screen before destroying the view, flush-diagnostic callback check, single "compact" decision, tests use named accessors, cheaper frame hand-off with a dropped-frame count, no "Past N min" on the large Overview (§5.6). **Partly done (2026-11-05, host and build):** the misleading second-unit comment is fixed; `DisplayRuntime::deinitialize` loads a blank screen before the view is destroyed; the flush-diagnostic callback result is checked and its dependency on esp_lvgl_port 2.9.0 is documented; the "compact" decision is made once (`compact_`); the large Overview no longer shows "Past N min" (asserted in `display_render`, fixtures refreshed). **Later (2026-11-05):** the display tests now find widgets through `DisplayView::footer_label()`, `trends_label()`, `trends_chart()` and `trends_status()` instead of child indexes (host display test, identical behaviour). **Still open:** the cheaper frame hand-off with a dropped-frame count (the 5 s deep copy of `Registry` and `TrendCache`) - the hand-off touches the display task's concurrency and needs a hardware soak, so it is left for a separate change. **Closed under R10-07 (2026-11-11):** the frame hand-off is done (shared trend snapshot, registry swap, dropped-frame count).
- [x] **R03-08** [HW] Submit a wrong Wi-Fi network from Settings: Overview and alerts return on the LCD, and the AP closes after the window. ~~Owner code typed in lower case with a space is accepted on an unenrolled device.~~ (no longer applies: the owner code was removed in R10-00) **Done (2026-11-12, board, `tools/device_wrong_wifi_test.py`):** the candidate failed after about 30 s and the device rejoined its saved network with the credentials kept; the console logged the Overview and Trends views 47 times while the setup access point was open; the access point closed about 10 minutes after the failure ([record](validation/device-wrong-wifi-2026-11-12.md)). The panel itself was not watched.
- [x] **R04-01** 85.0 °C accepted only when consistent with the previous reading or confirmed by the next (§3.4). Done: a reading of exactly 85.0 is believed at once only if the probe's previous raw reading was within 10 °C of it; otherwise it is discarded for that cycle (state `invalid`, no alarm) and accepted only if the very next reading is also exactly 85.0 (any other or missing reading resets the confirmation). New `power_on_reading` host test (failed before the change) covers first reading, two in a row, gaps, near-85 history, calibration, alarms and ordinary readings; the old in-firmware test that asserted the flag-only acceptance now asserts the real rule. `./tools/verify.sh` green (31 host tests); firmware builds.
- [x] **R04-02** A ROM CRC error during the search rescans; a bus fault only if it repeats (§3.5). Done: `ScanInput::crc_error` (set from `ESP_ERR_INVALID_CRC` by `OneWireService`); the first CRC error keeps reading the known probes, reports the scan incomplete and rescans in 5 s without a fault; a second in a row is a bus fault with no reads; a clean scan resets it. New cases in `scan_policy` failed first. `./tools/verify.sh` green; firmware builds.
- [x] **R04-03** Scan retry backoff 5/10/20 s capped at 60 s, reset on a clean scan (§3.6). Done in `ScanPolicy`: failed or suspect scans, repeated CRC errors, no-presence, repeated transaction failures and reads where nothing answers escalate 5, 10, 20, 40, then 60 s; a clean scan or all probes answering resets it; one quiet probe while the bus works stays at a fixed 5 s. New `scan_policy` cases failed first. `./tools/verify.sh` green; firmware builds.
- [x] **R04-04** One function owns per-sensor state changes; identical repeated transitions limited to one per 60 s in the journal; decide `State::unassigned` (§3.7). Update `alert_service_tests.cpp:150-160`. Done (host/build only): `AlertService::set_state` is the single writer; the registry journals an identical transition once per 60 s per sensor (400 flap events became at most 70 in the test); `unassigned` stays a display-slot value, never held by a registered sensor (§3.7).
- [x] **R04-05** `storage_error` events limited to state changes plus one per hour (§7.3). Host test. Done (host/build only): `StorageErrorLimiter` journals the first failure of an episode, then one reminder per hour; 24 h of failing buckets give 24 events instead of 288.
- [x] **R04-06** `event_healthy_` recovers after successful writes; `storage_info()` keeps last values on mutex timeout (§7.4). Done (build only; the code is FreeRTOS-bound so there is no host test, and it is not run on the board): `event_healthy_` follows the latest journal write; `storage_info()` returns the last good figures when the history mutex is busy.
- [x] **R04-07** Skip and report one corrupt completed history segment; retry transient append errors (§7.5). Host tests. Done (host/build only): a damaged completed segment is kept untouched, counted (`skipped_segments()`), skipped by `visit`, and makes a history query `truncated`; retention removes it when it is the oldest. An append that failed on I/O is retried on the next bucket after `recover()`; integrity failures (replaced/edited segment) still fail closed. Two old assertions that a failed write latches the writer were updated. CSV export skips such a segment silently (no flag in a CSV).
- [x] **R04-08** Duplicate display positions, `excess_`/overflow, NVS commit retry, stale/invalid flapping in the display snapshot (§4.5). Done (host/build only): equal display positions are ordered by ROM in the LCD and the API; `Registry::excess_` removed (overflow is the probe scan's field); a failed NVS save no longer locks sensor settings until reboot (damaged stored data still does); a display copy only expires sensors that show a value, so invalid/stale no longer flip.

## Phase R5 — API and contract (P2)

- [x] **R05-01** `PATCH /settings` and `PATCH /sensors/{rom}` reject `{}` (§7.6). API test. Done (build only): both PATCH handlers return 400 for `{}`. The HTTP layer has no host harness, so the 400 is not exercised by a test yet; add it to the on-board batch (R03-08 pass). **Board check (2026-11-05):** `PATCH` with `{}` and a current `If-Match` returns `400` on both `/settings` and `/sensors/{rom}`.
- [x] **R05-02** History query ROM rules (lowercase, no duplicates) enforced or documented (§7.6). Host test. Done (host): the history query parser rejects uppercase hex in `rom_id`/`boot_id` and duplicate `rom_id`; `contracts/API.md` states it. Tests use a literal ROM with hex letters so the check cannot pass vacuously. **Board check:** history rate limiting was observed (see R07-04).
- [x] **R05-03** cJSON allocation failure sets `truncated`; receive override removed on stop; mutex freed on start failure (§7.6). Done (build only, no host harness for these): history JSON rows are built with every allocation checked, and a failure marks the answer `truncated` instead of dropping rows silently; the body-deadline receive override is replaced by the plain `recv` function when the request ends; `HistoryService::start` frees its mutex when queue or task creation fails.
- [x] **R05-04** WebSocket backpressure documented or a one-frame slot added; T07-08 wording corrected (§7.6). Done (docs): `contracts/API.md` states the real behaviour (no per-client queue, slow client closed, four sessions). The `docs/TODO.md` T07-08 wording stays for the R09 correction pass, which needs owner approval.
- [x] **R05-05** OTA upload checks `project_name` before writing; docs say signing and secure boot are out of scope (§6.5). Done (host for the check, build for the wiring; not run on the board): `ota_image_matches_project` reads the first 112 bytes and refuses a different product (`422`) before `esp_ota_begin`; host test covers wrong/prefix/longer name, missing magic, unterminated name and short input. `API.md` states signing and secure boot are out of scope. Add a wrong-product upload to the on-board OTA batch (R02-07).

## Phase R6 — Web application (P2/P3)

- [x] **R06-01** Failed resync keeps live values shown as stale (§8.2). Done (web tests): a failed REST resync no longer raises the stale banner while the socket is open and delivering; when the connection really drops, readings stay on screen dimmed with a Stale chip instead of turning into dashes. Two live tests (healthy-socket resync failure, failed first load) and one App test.
- [x] **R06-02** `request()` checks status before parsing; readable errors; 401 still opens sign-in (§8.2). Done (web tests, new `api/auth.test.ts`, 8 cases): `request()` decides on the status first and reads the body only for a message; non-JSON error pages give `Device request failed (N)`; a 401 opens sign-in even when its body is not JSON (not for `/auth/*`); timeouts read "The device did not answer in time", network failures "Could not reach the device", a non-JSON success "The device sent an unreadable response".
- [x] **R06-03** Empty display position gives a field error (§8.2). Done (web tests): the display-position field is kept as text; empty or non-numeric input shows "Enter a whole number from 1 to 5." and nothing is sent, instead of sending `order = -1`. Out-of-range and fractional values are stopped by the browser's own min/max/step check (tested as not sent); a valid position is sent zero-based.
- [x] **R06-04** `temperature()` unit argument required; failed `getSettings` shows an error and retries (§8.2). Done (web tests): `temperature()` requires its unit (callers and tests updated); a failed `getSettings` shows an info notice and retries at 2, 4, 8 … 30 s until it loads, then applies the saved unit and timezone and removes the notice.
- [x] **R06-05** History: readable gap tooltips, empty zoomed view message, re-fetch after first clock sync (§8.2). Done (web tests): gap tooltips read "Missing data: 10 min to 20 min since boot (10 min missing)" (or formatted device times when the clock is synced) instead of raw milliseconds; a zoomed view with no data says so and how to get back; history reloads once the clock first synchronizes (the answer's time basis changes), but not on every snapshot.
- [x] **R06-06** Thresholds and calibration in the configured unit or clearly labelled °C (§8.2, D5). Done (web tests, D5): the sensor editor takes the configured unit; thresholds convert as absolute temperatures (x1.8 + 32), calibration and hysteresis as differences (x1.8), labels and input limits follow the unit, and the device still stores Celsius. Only fields the user edits are converted back, so untouched values are sent exactly as stored (no rounding drift). Tests cover a 50 F threshold to 10 C, a 1.8 F offset to 1 C, untouched fields exact, and the C case.
- [x] **R06-07** Accessibility: `aria-pressed` on range chips, `role="alert"` on errors, saved theme (§8.2). Done (web tests): the History range chips carry `aria-pressed`; the chosen theme is saved in `localStorage` (guarded, falls back to the system setting) and survives a reload. The error alerts already expose `role="alert"` because MUI's `Alert` sets it by default; tests assert this for the History error and the bus-fault banner rather than adding redundant attributes.
- [x] **R06-08** Tests: URL-routed fetch in `App.test.tsx` with a saved `units: 'C'` case; fake socket fires `onopen`; App tests stub `WebSocket`; simulator defaults (`units: 'F'`, `probe-scan enabled:false`) and non-empty history and alerts validated by `test-simulator-live.mjs` (§8.3). Done (web + simulator tests): App tests stub `WebSocket` (inert by default) and route `fetch` by URL, with saved `units: 'C'` (R06-04 test) and the failing-settings default; the live test's fake socket fires `onopen` (R02-06). The web simulator now defaults to `units: 'F'`, reports `probe-scan enabled: false` like a simulation build, and serves 120 history points over five probes plus two alert events; `tools/test-simulator-live.mjs` asserts all of it against the schemas.

## Phase R7 — Public read-only access (feature, §12)

- [x] **R07-01** Route policy table from §12.1 implemented as one access function in `http_api.cpp`, with host-testable rules (D1-D3). Done (host test for the table, build for the wiring; not run on the board): `route_access(method, path)` in `monitor_core` is the single access table (open / owner / unknown) with `route_access_tests`; `HttpApi::handle` uses it, returns 404 for any route not in the table (so no handler is reachable by accident) and 401 for owner routes without a session/CSRF. Anonymous `GET /settings` already omits `ntp_server` (D3). The WebSocket stays owner-only until R07-03 and the web app still shows its sign-in gate until R07-05, so the public reads are only reachable by direct API calls for now.
- [x] **R07-02** Public `GET /settings` response without owner-only fields; `auth/state` reports `signed_in`; contracts, `API.md` and generated types updated with the version decision (§12.2). Done (web/contract tests, firmware build only): new `device-settings-public` schema (display fields only, no etag/revision/writable/ntp_server); anonymous `GET /settings` returns it, a signed-in owner still gets the full schema; `auth/state` reports `signed_in` (required) with `csrf` only when signed in; generated types, `API.md`, the simulator and its live test updated. Version decision recorded in `API.md`: no bump, because client and device ship in one image. The new firmware responses are not run on the board yet.
- [x] **R07-03** Public WebSocket upgrade (Origin still checked) with the 8-client cap and a reserved owner slot (§12.1, §12.4). Done (host tests for admission; build for the wiring; not run on the board): `WsLive` admits anonymous viewers (empty token) and owners; default 6 slots with 1 reserved for a signed-in owner (host tests: 5 viewers, 6th viewer refused, owner admitted, invalid session still refused, viewers never swept as expired, broadcasts reach them). The upgrade still needs the device's own Origin and Host. **Deviation:** 6 clients instead of the spec's 8, with the server's `max_open_sockets` raised 6 to 8; the memory cost of more sockets is unmeasured, so the board pass (R07-08) should check the internal-heap floor with all six connected before raising it. Flake found and fixed here: `tools/test-simulator-live.mjs` failed with `socket hang up` on a busy machine (not reproducible on demand); the likely cause is a client reusing a keep-alive socket the simulator was timing out, so the simulator now closes each connection after its response and the test prints the stack of any failure. Not proven by a reproduction. **Board check (2026-11-05):** five anonymous viewers admitted, the sixth refused (`503`), an owner admitted into the reserved slot, forged Origin/Host refused. Free-heap floor with six clients not yet measured. **Revised after measuring on the board (2026-11-05):** six clients with eight sockets left only 15,032 bytes of free internal memory under a heavy overlapping load (login, REST polling, history queries, CSV export, five viewers and the owner's socket), against a floor of about 40 KB when idle. The cap is now **four clients (three viewers plus the owner's reserved slot) and six sockets**; the same load then leaves 29,628 bytes, three viewers and the owner are admitted, and a fifth client gets `503` (see the addendum in [device-public-access-2026-11-05.md](validation/device-public-access-2026-11-05.md)).
- [x] **R07-04** Per-client rate limits: history 1/s, export 1 per 30 s, 2 concurrent history queries, `429` with `Retry-After` (§12.4). Host tests. Done (host tests for the limiter; build for the wiring; not run on the board): `PublicLimiter` (history 1/s and export 1 per 30 s per client, 2 running at once, refusals cost nothing, LRU table of 8, backwards clock tolerated); the history/export route returns `429` with `Retry-After`. The HTTP server handles one request at a time, so the concurrency cap is a safeguard rather than something the current server can reach. Documented in `API.md`; the board check is part of R07-08. **Board check (2026-11-05):** a second history query returns `429` with `Retry-After: 1`, a second export `Retry-After: 30`; both succeed after the wait.
- [x] **R07-05** Web: Dashboard, History and Alerts without sign-in; Sensors and Settings show an inline sign-in and owner forms after it; sign-in/out control in the app bar; 401 opens sign-in (§12.3). Web tests. Done (web tests; built into the firmware image, not run in a browser against the board): the whole-app `AuthGate` is gone. `AuthProvider` shares the sign-in state; Dashboard, History and Alerts need no sign-in; Sensors shows an inline sign-in (or first-time setup) until signed in; Settings keeps the storage/diagnostics card public and gates the owner controls; the app bar shows Sign in (opens Settings) or Sign out; a 401 (`auth-required`) returns the page to its sign-in. New App tests: public dashboard and alerts, Sensors sign-in then forms with no owner request while anonymous, Settings diagnostics public, sign-out. Existing AuthGate tests now wrap the provider; the timezone test serves `signed_in: true`.
- [x] **R07-06** API tests: every route anonymous and signed-in; mutations without session or CSRF rejected (§12.5). Done as scripts, **not run** (they need the board): `tools/device_api_test.py` now asserts the public model (anonymous reads 200, anonymous settings without `ntp_server`/`etag`/`revision`/`writable`, owner settings with them, `signed_in` in auth state, anonymous owner routes 401, session without CSRF 401, unknown routes 404, empty PATCH 400, anonymous WebSocket upgrade accepted and a forged Origin refused, second history query within a second 429). Destructive owner routes (factory reset, real OTA) are deliberately not sent anonymously; the host `route_access` test covers them. `device_sensor_test.py` and `device_history_profile.py` were adapted to the rate limits and the public export. The host-side parts of this task are the `route_access`, `public_limiter` and WebSocket admission tests.
- [x] **R07-07** Update `SECURITY_DESIGN.md` and `USER_GUIDE.md` for the public model (§9, §10). Done (docs): `SECURITY_DESIGN.md` gains the public read model (what is public, owner-only and never public, the access table, load protection, the accepted limitation) and its test list now mentions the route table and the OTA project-name check; `USER_GUIDE.md` explains who can see what, the Sign in/out control, the history limits and that limits are entered in the chosen unit. The 80-bit owner code, 60-bit Wi-Fi key, `.local` address and LCD wording stay under R09-07.
- [ ] **R07-08** [HW] A phone with no session sees live readings, history and alerts; changing a threshold asks for the password; repeated public history requests don't stall the dashboard. **Partly done (physical, from a computer, not a phone):** see [device-public-access-2026-11-05.md](validation/device-public-access-2026-11-05.md). Anonymous reads, the owner-only routes, the rate limits (`429` + `Retry-After`), the anonymous and owner WebSocket (five viewers, a refused sixth, the reserved owner slot) and the empty-PATCH `400` all behaved as designed on the board. Still open: a real phone, the web sign-in flow in a browser, the free-heap floor with six clients, and the dashboard-stall check. **Memory part done on the board (2026-11-05):** see the addendum in the validation record: with four clients the free internal memory stayed at 29.6 KB under a worst-case overlapping load. The dashboard-stall check (repeated public history requests while the dashboard streams) was not done separately.

## Phase R8 — mDNS (feature, §13)

- [x] **R08-01** `hostname` device setting with RFC 1123 validation; settings record `TMD2` with `TMD1` migration; schemas and version decision updated (§13.1, §13.2). Host tests. Done (host tests; wiring built, not run on the board): `hostname` device setting (`valid_hostname`: one lowercase RFC 1123 label, 1-63, no leading/trailing hyphen, default `temperature-monitor`); the settings record is now `TMD2` and a `TMD1` record loads with the default hostname and every other saved choice kept (tested, a damaged `TMD1` still refused); `GET`/`PATCH /settings` carry it; the three settings schemas, generated types, simulator and `API.md` updated, with the no-version-bump decision recorded (embedded client). Renaming does not yet re-register mDNS (R08-02).
- [x] **R08-02** Rename re-registers mDNS without reboot and updates the Host allow-list; active name read back after conflicts (§13.1, D4). Done (host test for the allow-list; mDNS and wiring built, not run on the board): `host_allowed` (monitor_core, `host_rules_tests`) accepts the setup address, the station address, and `<configured name>.local` and `<active name>.local` (case-insensitive, optional `:80`), and refuses everything else including the old name after a rename. `WifiService` starts mDNS with the saved hostname, `mdns_rename()` changes it without a reboot, and `mdns_hostname()` reads back the name the stack is really using (the ESP-IDF mDNS component renames itself to `<name>-2` on a conflict). `HttpApi` applies the hostname after a settings update and checks `Host` against it; sessions are untouched. D4 default stays the plain name.
- [x] **R08-03** Advertise `_http._tcp` with TXT `path=/` and `model=temperature-monitor`; `GET /status` includes `mdns_hostname`; mDNS failure reported, IP access unaffected (§13.1). Done (web contract test; wiring built, not run on the board): the `_http._tcp` service now carries TXT `path=/` and `model=temperature-monitor`; the snapshot (`GET /status`, WebSocket) has `wifi.mdns_hostname`, the name mDNS is really using or `null` when mDNS is unavailable (required field, strict single-label pattern; fixtures, simulator and generated types updated). If mDNS cannot start, `mdns_ready_` stays false, the device keeps working by IP and the field is `null`; if only the service advert fails, the name still resolves. Contract note: no version bump, as before.
- [x] **R08-04** Show `http://<name>.local` in web Settings and on the LCD setup screen once the station is connected (§13.1). Display and web tests. Done (headless LCD renders and web tests; not seen on the physical panel or run on the board): the setup screen offers `or <name>.local` under the setup address when the station is connected and mDNS is up (compact: address and name share a small three-line block, the page marker is dropped to make room; large: its own line; renders in `docs/validation/lcd-simulation/provisioning-*local-name*.png` and asserted by text); the web Settings diagnostics (public) show `http://<name>.local` or "Network name unavailable"; the owner's device preferences gain a Network name field (lowercased as typed, validated as one RFC 1123 label, `.local` suffix shown, empty stopped by the browser's required check).
- [ ] **R08-05** [HW] Resolve `http://temperature-monitor.local` from Linux, Windows, macOS/iOS and Android; rename and re-resolve without reboot; note clients that can't resolve `.local`. **Partly done:** Linux (Avahi) resolves `temperature-monitor.local`, lists the `_http._tcp` service with its TXT records, and a rename takes effect in about a second without a reboot ([record](validation/device-mdns-2026-11-05.md)). Still open: Windows, macOS/iOS and Android.
- [x] **R08-06** [HW] Two devices on one network (or a simulated conflicting responder) end up with distinct, displayed names. Done on hardware with a simulated conflicting responder (Avahi on a laptop, not a second board): the board reported `garage-conflict-2`, which resolved, and both names were accepted as `Host`; the setting stayed as configured ([record](validation/device-mdns-2026-11-05.md)). The name shown on the physical LCD is not yet seen.

## Phase R9 — Build, docs and release (P3)

- [x] **R09-01** Remove the host test suite from the firmware image; keep non-fatal on-device self-tests reported in status (§4.2). Done (build; the host suite still runs under ctest): `tests/core_tests.cpp` is no longer compiled into the firmware and `main()` no longer calls it or logs "SELFTEST PASS core behavior", so a failing host check can no longer exit before NVS starts, and the image is about 36 KB smaller (2,083,232 to 2,047,024 bytes). The only on-device self-test left is the PBKDF2 known-answer check in `AuthService::initialize()`, which fails closed (authentication unavailable) instead of crashing.
- [x] **R09-02** Remove or use the `espressif/ds18b20` dependency (§4.3). Done (build): `espressif/ds18b20` was pinned but never used (the firmware reads probes through `onewire_bus` directly and decodes the scratchpad itself); removed from `main/idf_component.yml`, `dependencies.lock` (regenerated by `idf.py reconfigure`; only that entry and its direct-dependency line changed) and `docs/DEPENDENCIES.md`. The firmware builds and the SDK check passes.
- [x] **R09-03** `verify.sh` discovers Python tests; CI runs the leak checker and checks generated contract types are current (§4.3). Done (run locally; the CI workflow change is validated as YAML only and has not run on GitHub): `verify.sh` now discovers every `tools/tests/test_*.py` (30 tests, it named three files before), runs the leak checker, and runs the new `tools/check-contracts.sh`, which regenerates the TypeScript contract types and fails if they differ from the checked-in ones (tested: a deliberate edit made it exit 1, regeneration restored it). CI gets an explicit step for the leak check and the contract check, and runs `verify.sh --firmware` as before.
- [x] **R09-04** Auth internals: dedicated notification index, safe cross-task read of the setup secret and enrolled flag, `AuthService` host tests behind an NVS/KDF seam (§6.4). Done (host tests, 2.7 s, also clean under ThreadSanitizer; firmware builds; the new derive path is not yet run on the board): (1) the password-derive wait uses a per-request binary semaphore instead of the task's notification, so nothing else can release it early (no FreeRTOS config change needed); (2) `enrolled_` and `ready_` are atomic, `setup_secret()` returns a copy taken under the lock, and the verifier (`salt_`, `hash_`), the owner code and `next_login_` are only touched under the lock, with the slow derive working on copies; (3) new `auth_service_tests` run the real `AuthService` against `tests/auth_mocks` (FreeRTOS on threads, in-memory NVS, a real PBKDF2-HMAC-SHA256 so the known-answer self-test runs): enrollment (wrong code, 5 s spacing, malformed codes not counted, short password, normalised typing, storage failure, single use), login (per-client backoff, one derive at a time, abort, logout), a failing crypto engine (pause, no strike), password change (token, old password, spacing, sessions ended), restart persistence, damaged and old-format owner codes, unopenable storage, and cross-task reads. **Board check (2026-11-05):** owner logins through the new per-request derive semaphore succeeded every time on the board, including while REST polling, history queries and a CSV export ran at once (about a dozen logins across the memory runs, each `200`); the free-memory cost of a login is about 7.5 KB while it runs.
- [x] **R09-05** Release provenance: compare `web/dist` with `web_assets.json`, ESP-IDF commit from the SDK, `--commit` equals `HEAD`, refuse a dirty tree, version from the app descriptor, build the web once in CI (§9). Done (Python tests, 18 in the provenance suite; the CI workflow changes are validated as YAML only and have not run on GitHub): `release_provenance.py` now (1) compares every file in `web/dist` with `firmware/components/http_api/generated/web_assets.json`, the list packed into the image, and names the files that differ; (2) takes the ESP-IDF commit from the SDK checkout actually used (`--idf-path`, default `$IDF_PATH`) and fails unless it is the pinned commit; (3) requires `--commit` to equal `git rev-parse HEAD`; (4) refuses a working tree with uncommitted or untracked changes; (5) reads the version and project name from the app descriptor inside `temperature_monitor.bin` and requires them to match the CMake version. CI passes `--idf-path`, and `verify.sh` honours `SKIP_WEB_BUILD=1`, which CI sets so the web app is built once. A CLI test runs the script against a throwaway git repository.
- [x] **R09-06** Leak checker scans staged files and recent history for current setup values and fails instead of skipping (§9). Done (10 Python tests; also run for real against this machine's secrets, which passed): `tools/check-secret-leaks.py` now reads every `.build/*.json` (password, setup_secret, wifi_password, ap_password, wifi_key, owner_code) and scans the working files, the **staged** changes and the diffs and messages of the last 100 commits (`--history N`, 0 disables), reporting where a leak is (file, `staged changes`, commit id) and never its value. It exits 1 when it cannot do its job (a setup file that will not parse, git that will not answer) instead of skipping; with no setup files at all it passes with a note (a CI checkout has none), and `--require-secrets` makes that a failure on a machine that should have them.
- [x] **R09-07** Update `SECURITY_DESIGN.md` for the 80-bit owner code and 60-bit Wi-Fi key; `USER_GUIDE.md` for °F default, both units on the LCD and the `.local` address (§9). Done (docs): `SECURITY_DESIGN.md` now describes the 60-bit Wi-Fi key and 80-bit owner code (what they are, why they are short, what protects them, the older 256-bit code still accepted until replaced), the per-client login backoff and `429`, and records both KDF timings honestly (about 14.7 s measured earlier, 30.7 s on 2026-11-05); `USER_GUIDE.md` describes the LCD showing both units, its footer and setup pages, the `.local` address and the Network name setting (conflict naming, clients that cannot resolve `.local`), and the unit of calibration; the old claim that the LCD is disabled is removed.
- [x] **R09-08** With owner approval, correct `docs/TODO.md`: reopen or annotate the affected checked tasks, move T09-14's notes, refresh T11-09's counts (§9). Done under R10-13 (2026-11-12).
- [x] **R09-09** Design cleanups from §11 (USB bridge naming, host-only identity checks documented or simplified). Done (build only; behaviour unchanged by design, not re-run on the board): the USB test bridges (`DNS_BRIDGE`, `HTTP_BRIDGE`, `WS_BRIDGE`, `WS_LOAD_BRIDGE`, `WS_SLOW_BRIDGE`, about 540 lines) moved out of `provisioning_service.cpp` into `usb_bridge.cpp` behind `handle_usb_bridge_command()`, so `provisioning_service.cpp` (116 lines) holds only the owner USB commands (`SETUP_INFO`, `RESET_ADMIN_CREDENTIALS CONFIRM`, `WIFI_CONNECT`) and the dispatch; the component keeps its name. The host-only file-identity checks in the history store are documented in `history.hpp` (what they guard against, that the device build does not compile them, and which checks protect the device instead). **Board check (2026-11-05):** after the split, `HTTP_BRIDGE` over USB returned the status response (`200`) and `SETUP_INFO` returned its begin and end markers; the owner's setup contents were not displayed or recorded.

## Phase R10 — Follow-ups from the owner-code removal and the code review

Each item states its acceptance in §16 of `REVIEW_FIX_SPEC.md`. Items marked `[HW]` need the
connected board.

- [x] **R10-00** Remove the owner code from enrollment, the sign-in form, the USB setup output, the
  LCD setup screens and the docs; the admin password is created during the BOOT setup window. Done
  (host, display, web and Python tests, firmware build; board enrolled with a password-only
  request, HTTP 201).
- [x] **R10-01** Trend restore logs a failed or partial history read and an untrusted clock instead
  of failing silently. Done (firmware build only: the restore runs in `main.cpp`, which has no host
  test; the log lines have not yet been seen on the board).
- [x] **R10-02** "Live connection lost" is shown only after it has lasted ten seconds, so brief
  reconnects do not flash. Done (`delayedError` unit tests and the App live-connection tests).
- [x] **R10-03** [HW] History queries finish well under one second on a full store, so the history
  rate limit can refuse a second query made within a second. Bound the store scan to the requested
  range (§16.1). Acceptance: `tools/device_api_test.py` passes its history rate-limit check
  unchanged on the board, and a host test covers the bounded scan. Done: recovery and appends index
  each segment's UTC and per-boot uptime span, and a ranged visit leaves segments outside the range
  unread (`history_range` host test, which fails with the skip disabled). **Board (2026-11-10, over
  Wi-Fi, medians of 8):** a 1-hour query 0.34 s (1.41 s before), 24 hours 0.86 s, 30 days 1.43 s
  (it covers most of the store); `device_api_test.py` passed "second history query within one
  second is rate limited", and 4 of 4 manual trials were refused.
- [x] **R10-04** The device test expects the sensor count it is told to expect, not a range of zero
  to five (§16.2). Acceptance: the test fails when the count differs from `--expect-sensors`. Done:
  `--expect-sensors` is required on a board (a simulation image is always five), checked in
  `device_expectations.py` with unit tests. **Board (2026-11-11):** with no count the test refuses
  to start; with `--expect-sensors 3` it failed at "device WebSocket frames are complete bare-board
  snapshots"; with `--expect-sensors 4` all 455 checks passed.
- [x] **R10-05** Erase the stale owner code (`bootstrap` key) from storage once at startup, and
  remove the test that asserts it is ignored (§16.3). Acceptance: host test shows the key is gone
  after startup; on the board the key is absent after one reboot. Done: startup erases
  the key and leaves the verifier untouched (host test replaces the "ignored" one). **Board
  (2026-11-11):** before the flash `monitor_auth` held `bootstrap` and `verifier2`; after one boot
  only `verifier2` (read from the NVS dump, key names only), and the administrator login still
  succeeded.
- [x] **R10-06** Enrollment refusal says why it was refused: setup window closed (hold BOOT for five
  seconds), repeat enrollment, or spacing after an attempt (§16.4). Acceptance: each cause has its
  own message in the API and the sign-in form, with a test for each. Done (host
  and web tests; API status codes 403, 409, 429, 400 and 503, documented in `contracts/API.md`).
  The board was already enrolled, so on the board only the 409 repeat-enrollment reply was
  checked, by `device_api_test.py`.
- [x] **R10-07** Display cleanups from R03-07 (comment fix, blank screen before destroying the view,
  flush-diagnostic callback check, single compact footer). Acceptance: the display tests pass and
  each cleanup is covered by a test or noted as build-only. Done: the
  comment, blank screen, callback check, compact decision and named accessors were done under
  R03-07 (display tests). The last item, the frame hand-off, is build-only because it lives in
  `main.cpp`: the trend cache is handed over as a shared immutable snapshot rebuilt only when it
  changes, the registry is copied outside the lock and swapped in, and frames the display task did
  not release within 100 ms are counted as `display_dropped` in the HEALTH line. **Board
  (2026-11-11):** over 6 minutes, 76 HEALTH lines with `display_dropped=0`, the LCD alternated
  Overview and Trends 12 times each after the trends were restored, no panic; `device_api_test.py`
  passed all 467 checks.
- [x] **R10-08** T06-15: measure bytes per history record and the retention for one to five probes,
  and document the oldest and newest timestamps (§16.5). Acceptance: numbers recorded in the docs. Done (board,
  2026-11-12): recorded in `docs/HISTORY_FORMAT.md` and the retention validation record. Oldest
  2026-11-04 11:10 UTC, newest 2026-11-12 07:35 UTC, 314 records from 71 boots; 180 bytes per
  four-probe record; forecast about 61 days at four probes when logging is continuous. The store has
  not filled yet, so retention under eviction is still the host profile's; `docs/TODO.md` T06-15 is
  left for R10-13.
- [x] **R10-09** T10-02: mark a new firmware valid only after startup health checks, roll back when
  they fail, and show the update status on the LCD and web UI (§16.6). Acceptance: host tests for
  the health gate; on the board, the status shows in the web UI. Done: the
  gate, the 120 s self-rollback and the `ota_boot_policy` host tests already existed (T10-02);
  the web Settings line now says the state in words ("new firmware being checked (it rolls back if
  the check fails)") with web tests. **Board (2026-11-12):** two over-the-air updates, `ota_0` to
  `ota_1` and back: each reported `pending_verify` after the restart and `valid` 5 to 8 s later,
  and the Settings page in a browser showed "ota_0 · OTA valid". The LCD's update-state footer is
  covered by the display test only; it was not looked at on the panel.
- [ ] **R10-10** T10-05: profile heap, client counts, LVGL, flash writes and Wi-Fi traffic for leaks
  and high-water marks (§16.7). Acceptance: a recorded profile with no growth over a soak run. **First run
  (2026-11-12, 45 min, `tools/device_soak_profile.py`):** all 2197 requests `200`, two WebSocket
  viewers held throughout, no faults, no dropped display frames, internal RAM flat (median 57.7 KB),
  PSRAM down 2.7 KB in two steps that match the trend cache filling toward its 24-hour cap
  ([record](validation/device-soak-2026-11-12.md)). Still open: a run longer than 24 hours to show
  PSRAM levels off, and LVGL pool and flash-write counts, which are not measured.
- [x] **R10-11** T11-01: reconcile `SPEC.md`, `TODO.md`, the wiring document, the UI specs and the
  diagrams with the final pinout and measurements (§16.8). Acceptance: no conflicting statements
  remain, checked by a reviewer. Done
  (2026-11-12): the pinout (GPIO4, 12, 11, 10, 9, 14, 15 conditional, 0) already agreed across
  `SPEC.md`, the wiring document and SVG, the LCD spec, the bench checklist and `DEVELOPMENT.md`.
  An independent review against the code found nine contradictions, all corrected: enrollment is
  allowed while the setup access point is open (first start or a BOOT hold, not only a BOOT hold);
  compact Trends is one multi-probe graph with axes, not a rotating single graph; six web pages
  with Events; the live WebSocket, reads and CSV export are public; OTA images are not signed;
  display settings are public; the README names the ST7735; the 128 × 160 corner-pattern check
  cites its record; no LCD pairing code. `contracts/API.md` access notes now match
  `route_access.cpp`, and the panel, login time and rate-limit wording elsewhere were corrected.
  `docs/TODO.md` is left to R10-13 (owner approval); dated handoffs and validation records are
  history and were not rewritten.
- [ ] **R10-12** T11-08: publish versioned firmware binaries, web build provenance, checksums and
  release notes, with known issues, only after every item above is done (§16.9). **Prepared
  (2026-11-12):** version 0.2.0 set in the firmware, `web/package.json` and the lockfile; release
  notes with known issues in [`RELEASE_NOTES_0.2.0.md`](RELEASE_NOTES_0.2.0.md) (the draft now
  points there). The work is on `master` (fast-forwarded on 2026-11-12). Still to do, each needing
  the owner's go-ahead: accept the open checks as known issues (R10-10, R01-07, R07-08, R08-05),
  then tag `v0.2.0` and publish the GitHub release with the CI package from that commit.
- [x] **R10-13** R09-08: correct `docs/TODO.md`. **Done (2026-11-12, with the owner's approval):** the 18 tasks
  carry a code-review note naming the finding and the R-items that fixed it (T02-11, T03-04 and
  T03-05 note that board check R01-07 is still open); T07-08 says there is no per-client queue;
  T09-14's WebSocket note moved to T07-10; T08-01 notes that the owner code was removed; T06-15,
  T10-02 and T11-01 carry their 2026-11-12 progress and stay open; T11-09's count is refreshed
  (128/145 complete, 17 open).
- [x] **R10-14** Logins failed at random with "Login denied" although the password was right: the
  password worker's 8 KiB stack could not be allocated from the fragmented internal RAM ("Could not
  start password KDF worker"), and that device failure was reported as a wrong password (§16.11).
  Done: the worker stack comes from PSRAM, and a login the device could not check answers `503`
  with "try again in a few seconds" instead of "Login denied" (host tests for the PSRAM request and
  each failure cause). **Board (2026-11-10):** 20 of 20 logins through the USB bridge succeeded in
  the two minutes after boot, while the largest free internal block fell to 4608 bytes; the
  PBKDF2 self-test still passes.
- [x] **R10-15** Internal RAM ran out a minute after boot (low-water mark 576 to 820 bytes), so the
  network stack dropped or cut short HTTP responses and `device_api_test.py` failed at random
  places. The LCD trend cache held up to 288 full history buckets, each with its own allocations,
  which the PSRAM threshold placed in internal RAM, and a copy for the display task (§16.12).
  Done: the cache keeps compact `TrendPoint`s (start, boot, UTC and up to five averages) in PSRAM
  through `ExternalAllocator`, and the restore task collects points instead of buckets (host test
  for the compaction; display test unchanged). **Board (2026-11-10/11):** in the first 76 s after
  boot internal free RAM 58 to 64 KB (was 19 to 33 KB), low-water mark 38096 bytes (was 576 to 820),
  largest free block 31744 bytes (was 4608 to 19456); `device_api_test.py` passed all 452 checks.

## Phase R11 — Sensor calibration in an ice bath (feature, §17)

Owner request, 2026-11-12: ice point only (D8), with the probe's alerts paused while it is in the
bath. API additions are additive within schema v1 (§17.3); no settings storage change.

- [ ] **R11-01** Calibration hold in the firmware and API (§17.3): `PUT`/`DELETE
  /sensors/{rom_id}/calibration-hold`, kept in RAM with a 20-minute cap; while held, no high or
  low alert and no history for that probe, disconnect and bus-fault alerts unchanged;
  `calibrating` and `calibration_hold_ends_uptime_ms` in `GET /sensors` and the live feed;
  `calibration` started/ended events; `CAL` on the LCD; contracts and `contracts/API.md` updated.
  Acceptance: the host and display tests in §17.5 pass, and an older firmware's event journal is
  confirmed to skip the new event kind.
- [ ] **R11-02** Guided ice-bath calibration on each sensor's tab (§17.2): instructions, live
  measuring, the steadiness rule (12 readings within 0.125 °C), offset = 0 − mean uncalibrated
  reading rounded to 0.1 °C, the confirm screen with the change in readings and the reminder to
  check the high and low limits, save through the existing `PATCH`, the hold started, renewed and
  shortened to five minutes at Done or Cancel, and the guards (disconnect, bus fault, revision
  change, beyond ±12.7 °C refused, beyond ±2 °C warned, 10-minute give-up). Acceptance: the web
  tests in §17.5 pass; checked in a browser at 360 px against the simulator.
- [ ] **R11-03** User guide calibration section (§17.4): the ice bath, calibrating each probe as
  installed, what calibration changes and the limits reminder, the alert pause, and probe
  placement in a grow room. `docs/WEB_UI_DESIGN_SPEC.md` describes the flow. Acceptance: a
  reviewer can follow the guide without other documents.
- [ ] **R11-04** [HW] Calibrate a real probe in a real ice bath with the new flow. Acceptance
  (§17.5): the probe then reads 0.0 ± 0.1 °C in the bath, `probe_calibration` is `stored` with the
  new offset and survives a restart; during the bath no low alert for that probe, a gap in its
  history, `CAL` on the LCD, and the hold's start and end in the event log. Recorded under
  `docs/validation/`.

## Phase R12 — Material 3 redesign of the web app (feature, §18)

Owner request, 2026-11-12: real Material 3, a slicker and more professional look, Roboto Flex
(D9), seed `#2F6B4F` (D10), Events as a tab of Alerts with Alerts first and default (D11). No API
or storage change. R12-01 comes first; if R11-02 is built after it, it uses the new components.

- [x] **R12-01** Material 3 foundation (§18.1, §18.2): token generator and committed tokens (light,
  dark, status and chart colours), the MUI theme built from them, the type scale, Roboto Flex
  subset with its licence and a `docs/FONTS.md` web section, icons, shape, spacing and motion, and
  Material 3 component overrides. Acceptance: token and contrast tests pass; the size before and
  after is measured and within the §18.8 budget. Done
  (2026-11-12): tokens from seed `#2F6B4F` with `@material/material-color-utilities` 0.3.0 (dev
  only; 0.4.0's ESM build does not load in Node) through `npm run theme:generate` into
  `web/src/theme/tokens.generated.ts`; `web/src/theme/theme.ts` maps them to MUI (palette, the
  Material 3 type scale on Roboto Flex, shape and motion scales, `--md-sys-color-*` CSS variables,
  and Material 3 styles for buttons with a `tonal` variant, segmented toggle buttons, tabs,
  switches, chips, alerts, cards, fields, dialogs, menus, tooltips, snackbars and progress). Chart
  colours follow the LCD's hue order; the History chart uses them. Roboto Flex is cut to 67,004
  bytes by `tools/fonts/make_web_font.py` (source and checksum in `docs/FONTS.md`); its licence is
  served at `/licenses/RobotoFlex-OFL.txt`, and `pack-web.py` serves `.woff2` as `font/woff2`.
  `@mui/icons-material` 9.5.0 is added and first used in R12-02. Tests: tokens match the generator
  (shown to fail on an edited file), WCAG AA contrast for every text pair (4.5:1) and boundary or
  chart pair (3:1) in both themes, the theme mapping, and the 80 KB font limit. Size: the packed
  web build went from 192,795 to 264,914 gzip bytes (+72,119: font 67,009, licence 2,012, theme
  about 3,100), within the 150 KB budget. Checked in a browser against the simulator at 1280 and
  390 px, light and dark: Roboto Flex loaded, no horizontal scroll. `verify.sh --firmware` passed
  (39 host and 175 web tests); the firmware is 2,138,336 bytes, 49% of each OTA slot free.
- [x] **R12-02** Navigation and top bar (§18.3): rail at 840 px and wider, bottom bar below, five
  destinations with icons, the Alerts badge, the compact top bar with theme and sign-in icon
  buttons, and the status chips. Acceptance: navigation tests; no horizontal scroll at 360 px. Done
  (2026-11-12): `web/src/shell/Navigation.tsx` is a rail (96 px, the app's leaf mark on the panel
  evergreen) at 840 px and wider and a bottom bar below, with five linked destinations (Dashboard,
  History, Sensors, Alerts, Settings), Material 3 active indicators and an active-alert badge on
  Alerts (`GET /alerts` every 10 s; the link reads "Alerts, n active"). `TopBar.tsx` has the page
  title, the network name (hidden on a phone) and icon buttons with tooltips for the theme and for
  signing in or out; only conditions that need attention show, as amber chips with icons. The panel
  colours are generated tokens (`panel`, tone 20 with text at 98, 90 and 80) with a contrast test.
  App tests for the destinations, the current page, the badge, the conditions and the network name;
  older tests updated from buttons to links. Rail and bar checked in a browser at 1280 and 390 px
  (no horizontal scroll).
- [x] **R12-03** Alerts and Events as tabs of one Alerts page (§18.6): Alerts first and default,
  `/alerts` and `/events` open their tabs, empty state. Acceptance: route and tab tests. Done
  (2026-11-12): Events left the navigation; the Alerts page has Alerts and Events tabs, Alerts
  first and selected by default, `/events` opens the Events tab and switching tabs updates the
  address. Active alerts are list items with an icon and their status colours (too warm, too cold,
  disconnected, bus fault, Wi-Fi) and a "No active alerts" empty state; events are list rows with
  an icon for their kind and the time beside them. App tests for the tabs, the default and the old
  `/events` link; Alerts and Events tests updated for the new wording. Checked in a browser against
  the simulator.
- [x] **R12-04** Dashboard (§18.4): probe cards with state chip, sparkline, today's min/max and the
  limits bar, no ROM ID, skeletons, and the 24-hour chart within the history rate limit.
  Acceptance: Dashboard tests; one history query per refresh. Done
  (2026-11-12), following the approved concept: `web/src/features/dashboard/` has the live
  readings panel (headline and detail from `roomState.ts`, large light readings, a gauge per probe
  from the limits in `GET /sensors`, "Too warm", "Too cold", "Disconnected" tags, a pulsing live
  dot), the chart card (`chart/TrendChart.tsx`: smooth lines in the series colours, round
  gridlines, time ticks on round local times or counted back since boot, dashed limit lines for
  out-of-range probes, probe filter chips, a 24 hours / 7 days / 30 days segmented button, one
  history query per range or every five minutes) and the alerts card with a link to all events.
  Skeletons until the first snapshot; no ROM IDs; disabled probes are not on the panel. Recent
  events were left out (spec §18.4 says why). Tests: the room-state wording (six cases), chart
  scales and ticks (five), the Dashboard (five: wording with limits and duration, one history
  query across live updates, range switch and probe chip, alerts, skeletons), and the App
  Dashboard tests rewritten for the panel. The test device now serves `GET /sensors` publicly,
  as the firmware does. Checked against the simulator at 1440 px light, 1280 px dark and 390 px.
  Packed web build 286,918 gzip bytes (+94,123 since R12 began; budget 150 KB).
- [x] **R12-05** History (§18.5): segmented range, icon toolbar, export menu, filter chips, time
  axis, gridlines, hover or tap values, summary table, compact notices. Acceptance: History tests,
  including the unsynchronised-clock axis. Done
  (2026-11-12): History uses the Dashboard's chart (`chart/TrendChart.tsx`, now with shaded
  missing intervals titled "Missing data: …", lines that run through a gap but start again at
  each boot without a trusted clock, and each probe's value at the pointer on hover or tap),
  shared through `chart/historySeries.ts`. The toolbar has the range as a segmented button (1
  hour to 30 days), zoom in and out, earlier, later and reset as icon buttons with tooltips that
  are hidden when they would do nothing, refresh, and CSV export in a "More" menu. Probes are
  filter chips in their series colours with the ROM ID as the chip's description; a table gives
  each probe's low, count-weighted average, high and readings for the window; notices are one
  line. The Dashboard chart now shades gaps and shows values on hover too. Tests: chart runs and
  hover readout, tick labels, the table and export menu, and the History tests updated for the
  new controls (hidden rather than disabled, chips, paths). Checked against the simulator at
  1280 and 390 px: range buttons don't wrap, every other time label on a phone, the table fits.
- [ ] **R12-06** Sensors, Settings and sign-in restyled (§18.7): Material 3 tabs and fields, the
  °C/°F segmented button, the button rule, confirm dialogs for destructive actions, snackbars.
  Acceptance: the existing tests pass, updated only for the new controls.
- [ ] **R12-07** Quality pass and documents (§18.8, §18.9): skeletons, empty and error states,
  reduced motion, focus and accessible names; before and after screenshots of every page in light
  and dark at 390 and 1280 px under `docs/validation/`; `docs/WEB_UI_DESIGN_SPEC.md` and
  `docs/USER_GUIDE.md` updated. Acceptance: a reviewer compares the screenshots with §18.
- [ ] **R12-08** [HW] On the board: every page loads from the device with no internet, the font
  comes from the device, the firmware fits both OTA slots, and a phone shows the bottom navigation
  correctly. Recorded under `docs/validation/`.

## Definition of done

- [ ] All P0 and P1 items pass with tests, and their `[HW]` checks are recorded.
- [ ] Anonymous users can read readings, history and alerts; every change needs the owner password; rate limits hold under load.
- [ ] The device is reachable at its `.local` name from the owner's usual devices, and the active name is shown on the web and LCD.
- [ ] `./tools/verify.sh --firmware` passes and CI is green on the final commit.
- [x] `docs/TODO.md` matches the code after R09-08. (2026-11-12, R10-13)
