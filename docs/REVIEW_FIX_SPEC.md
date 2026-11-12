# Review Fixes and Access Changes — Specification

**Date:** 2026-11-05
**Checklist:** [`REVIEW_FIX_TODO.md`](REVIEW_FIX_TODO.md)
**Source:** the 2026-11-05 code review of all eleven phases of [`TODO.md`](TODO.md), plus two owner requests: dependable mDNS access, and public read-only access to readings and history with only settings protected.

This document says what must change and how each change is judged done. File and line references point at the code as of commit `82204b3`. Findings marked **verified** were re-checked against the code during the review; the others are reviewer findings that were read but not re-checked, and each fix must confirm the defect (ideally with a failing test) before changing code.

The project rules in `AGENTS.md` and `CLAUDE.md` still apply: host-testable logic, `./tools/verify.sh` before integration, deliberate decisions for API, schema and storage changes, and separate evidence for host, simulated and physical checks.

## 1. Goals and non-goals

**Goals**
- Fix every defect found in the review, highest data-loss and safety risk first.
- Make anonymous LAN users able to see live readings, history and alerts, while every change to the device still needs the owner password.
- Make the device reliably reachable at a `.local` name, configurable by the owner.
- Bring tests and documents back in line with the code, so checked TODO items are true.

**Non-goals**
- No change to the five-probe, one-pin hardware design.
- No cloud service, no TLS (the accepted LAN-only HTTP limitation stays; see §10).
- No change to the history record layout (TMH2) beyond what §3.1 needs, which is none.

## 2. Priorities

| Priority | Meaning | Items |
| --- | --- | --- |
| P0 | Silent data loss or a wrong reading/alarm | §3.1, §3.2, §3.3, §4.1 |
| P1 | Device stuck, unrecoverable or easily stalled | §5, §6.1, §6.2, §6.3, §7.1, §7.2 |
| P2 | Wrong or misleading behaviour with a workaround | §3.4-§3.7, §5.2-§5.5, §7.3-§7.6, §8, §9 |
| P3 | Code quality, tests, docs | §4.2-§4.5, §10, §11 |

The two new features (§12 public read access, §13 mDNS) are P1 for the owner and should start after the P0 fixes.

## 3. Sensor data and alerts

### 3.1 History buckets are dropped for calibrated probes (P0, verified)
- **Problem:** `Aggregator` stores `sum / count` as the average (`monitor_core/monitor.cpp:21`); `encode_bucket` rejects `minimum > average || average > maximum` on raw doubles (`monitor_core/history.cpp:183`). With a non-zero calibration offset, sixty equal samples average to one rounding step outside `[min, max]` (2,911 of 3,000 cases in a reproduction). The bucket is lost, history becomes unhealthy and a storage error is logged.
- **Required:** the finished average is clamped to `[minimum, maximum]` when the bucket is built. `encode_bucket` keeps its validation for genuinely bad data.
- **Tests:** host test with calibration offsets (e.g. 0.1, -0.3, 1.7) over steady and varying readings: every bucket encodes and round-trips; an average outside the range by more than rounding is still rejected.

### 3.2 A bus fault hides active alerts (P0, verified)
- **Problem:** `AlertService::update` writes `State::bus_fault` directly (`alert_service/alert_service.cpp:26-30`). After the fault clears, a sensor still over its limit goes `bus_fault → normal`, then raises `high` again only after `persistence_ms` (15 s default, up to 1 h). During that window an over-limit sensor reports `normal`, and the alarm history shows a fresh raise with no clear.
- **Required:**
  - A sensor remembers its state when a bus fault begins (`Sensor::state_before_fault`) and resumes from it when the fault clears, so an alarm episode, its persistence and its hysteresis are not split by the fault. Entering the fault stays silent for the sensor; the registry's bus-level event (rom 0) is the single record of the fault, once per fault.
  - On recovery, a sensor still beyond its limit (with hysteresis) stays in alarm with no new persistence wait and no new event. A sensor that is back within limits produces the normal clearing event (previous state = the pre-fault alarm). A sensor that was normal and returns over its limit still has to persist before alarming.
  - Disabling a sensor during a fault leaves it disabled.
- **Tests:** high → bus fault → recovery still high (no `normal` in between, no duplicate raise); high → bus fault → recovery normal; disconnected → bus fault → recovery.

### 3.3 A truncated ROM search is treated as complete (P0)
- **Problem:** the pinned onewire driver returns `ESP_ERR_NOT_FOUND` both at the true end of a search and when no device answers mid-search (`onewire_device.c:57-59, 81-83`). `onewire_service.cpp:48` counts both as complete, so dropped probes are removed from the read list, are no longer read, and become `disconnected` after 15 s although present. The same path turns "all probes vanished" into one disconnect per sensor (SPEC table row 297 wants a bus fault).
- **Required:**
  - A scan that finds fewer probes than the registry has enabled and recently seen is treated as *suspect*, not complete: keep reading the previous ROM list, rescan within 5 s, and accept the smaller set only after two consecutive scans agree.
  - A scan that finds no device at all while two or more probes are being read is a bus-level fault (one event), not N disconnects. The read list is kept so reading resumes the moment the bus does, and the fault is rechecked every 5 s. Empty scans that persist for 5 minutes are believed (the probes have really been removed) and the sensors then become `disconnected`. With only one probe being read, an empty scan is that probe being unplugged: it is confirmed by a second scan and shown as `disconnected`, never as a bus fault.
  - The scan-and-fallback decision moves into a pure, host-testable function (see §4.4).
- **Tests:** host tests with a fake iterator: mid-search drop, two agreeing smaller scans, all-missing, recovery.

### 3.4 Power-on 85 °C can be accepted (P2)
- **Problem:** the driver always passes `conversion_complete=true` (`onewire_service.cpp:103,108`). A probe that browned out after Convert T returns 85.0 °C (`0x0550`) with a valid CRC.
- **Required:** a reading of exactly 85.0 °C is accepted only if the same probe's previous valid reading was within 10 °C of it, or the next read confirms it. Otherwise it is treated as invalid for that cycle. The existing host test that asserts 85 °C is accepted when the flag is set is replaced by tests of the real rule.

### 3.5 One bad ROM CRC faults the whole bus (P2)
- **Problem:** `ESP_ERR_INVALID_CRC` during enumeration breaks the loop and becomes a bus fault for every sensor (`onewire_service.cpp:43-60`).
- **Required:** a CRC error during the search triggers a rescan; it becomes a bus fault only if it repeats on the next scan. Already-known probes keep being read meanwhile.

### 3.6 Scan retries have no backoff (P2)
- **Required:** after a failed or suspect scan, retry at 5 s, 10 s, 20 s, capped at 60 s, resetting on a clean scan (SPEC asks for backoff).

### 3.7 Duplicate events and state authority (P2)
- **Problem:** state changes happen in several places (`AlertService::transition`, the direct write above, `Registry::sample`, `Registry::expire`, `Registry::configure`). A flapping sensor with `persistence_ms=0` can emit 400 events (asserted by `alert_service_tests.cpp:150-160`).
- **Required:** all per-sensor state changes go through one function. Repeated identical transitions for one sensor are limited to one per 60 s in the event journal (the live snapshot still shows the current state). `State::unassigned` is either assigned to sensors as specified or removed from the sensor state set and kept as a display-slot value only.
- **Built:** `AlertService::set_state` is the only writer of a sensor's state; `Registry::record` journals an identical (sensor, previous, state) transition at most once per 60 s. `State::unassigned` stays in the enum and the contracts (so no schema change) as a display-slot value only: no registered sensor is ever in it (tested).

## 4. Build, configuration and test structure

### 4.1 Release builds don't read sensors (P0, verified)
- **Problem:** `CONFIG_MONITOR_SENSOR_BUS` defaults to `n` (`main/Kconfig.projbuild:17-20`) and `sdkconfig.defaults` doesn't set it, so CI artifacts and clean builds never read probes. Saved sensors then go `disconnected` and raise alarms.
- **Required:** `sdkconfig.defaults` enables the bus for the normal image. Bare-board tests that need the bus off use a separate defaults file (e.g. `sdkconfig.bareboard`). With the bus disabled, sensors show a distinct "bus disabled" state and raise no disconnect alarms. A failed `onewire_new_bus_rmt` at boot is a bus fault, not silence.
- **Tests:** CI builds the normal image and checks the resolved `sdkconfig` has `CONFIG_MONITOR_SENSOR_BUS=y`.

### 4.2 The host test suite runs at every boot (P3, verified)
- **Problem:** `main/CMakeLists.txt` compiles `tests/core_tests.cpp` into the firmware and `main.cpp:91` calls `run_core_tests()`; a failing check calls `exit(1)` before NVS starts (reboot loop), and "SELFTEST PASS" is logged even though the result is ignored.
- **Required:** remove the host suite from the firmware image. Keep only fast on-device self-tests whose failure is logged and reported in status, never fatal.

### 4.3 Dependencies and verification script (P3)
- Remove the unused `espressif/ds18b20` pin from `main/idf_component.yml` and `dependencies.lock`, or use it.
- `tools/verify.sh` discovers Python tests (`unittest discover`) instead of naming three.
- CI runs `tools/check-secret-leaks.py`, and checks that `npm run contract:generate` output is up to date (`git diff --exit-code`).

### 4.4 Host-testable 1-Wire logic (P3)
Scan scheduling, suspect-scan handling and fault decisions (§3.3, §3.5, §3.6) move out of `OneWireService` into `monitor_core`, with host tests. `OneWireService` keeps only the ESP-IDF calls.

### 4.5 Misc correctness (P3)
- Duplicate display positions: `PATCH /sensors` rejects an `order` already used by another enabled sensor, or the registry resolves ties deterministically and the web shows it.
- `Registry::excess_` is either wired to the probe-scan `overflow` field (a sixth probe while five are registered sets it) or removed.
- An NVS commit failure no longer locks sensor settings until reboot: retry on the next change.
- The display snapshot no longer alternates between stale and invalid every 100 ms for a probe with persistent CRC errors (`main.cpp:66`).

## 5. Wi-Fi setup mode and LCD

### 5.1 Setup mode can replace the Overview indefinitely (P1, verified)
- **Problem:** after a failed Wi-Fi candidate both AP deadlines stay 0, so `WifiApWindow::should_close` never returns true (`wifi_ap_window.hpp:37-45`, `wifi_service.cpp:429-440`). The LCD shows only the setup screen (no temperatures or alerts) until reboot. A never-provisioned device never shows the Overview.
- **Required:**
  - Every AP opening has a deadline: a failed candidate leaves the AP open for 10 minutes, then closes it once the station is connected again. BOOT recovery keeps its 10-minute window. An unprovisioned device keeps the AP open but alternates the LCD between Overview and setup.
  - While the AP is open, the LCD alternates setup and Overview every 10 s, so readings and alerts stay visible (T04-09, T04-10).
- **Tests:** host tests of `WifiApWindow` for failed candidate, reconnect, BOOT plus candidate, unprovisioned; display test that alternation happens and alerts show on the Overview page.

### 5.2 Compact footer drops logging health (P2)
`display_view.cpp:273-282` replaces the compact footer with `"Wi-Fi " + state`, hiding `Log ERR`. The compact footer shows logging failure with priority over the Wi-Fi state (for example `Log ERR`), and the Trends footer keeps its lookback text otherwise.

### 5.3 BOOT progress is hidden during a bus fault (P2)
While BOOT is held, `Hold BOOT NN%` takes priority over `BUS FAULT` in the footer.

### 5.4 Setup screen appears up to 5 s late (P2)
The display task reads the provisioning flag directly instead of waiting for the 5-second frame, so the setup screen appears within 200 ms of the hold reaching 100%.

### 5.5 Owner code entry is exact-match only (P2)
Firmware and web normalise the owner code before comparison: strip spaces, hyphens and line breaks, and convert to upper case. The alphabet omits `0`, `1`, `I` and `O`; a code containing them is rejected with a clear message and does not count towards the lockout (decision D7). Normalisation happens on the device (`auth_service.cpp:185`), so any client benefits. The web field is a visible text field with a hint, not a password field.

### 5.6 Minor display items (P3)
- Fix the misleading comment at `display_view.cpp:86-87`.
- `DisplayRuntime::deinitialize` loads a blank screen before destroying the view (`display_runtime.cpp:416`).
- The flush diagnostic checks the result of replacing the port callback, and the code notes that it depends on esp_lvgl_port 2.9.0's callback.
- Move the "compact" decision to one place; tests stop relying on child indexes (use named accessors).
- The deep copy of `Registry` and `TrendCache` every 5 s (`main.cpp:307-308`) is replaced by a cheaper hand-off (swap or shared immutable snapshot), and dropped frames are counted.
- "Past N min" is not shown on the large Overview footer.

## 6. Recovery, OTA and authentication

### 6.1 Bad auth data makes the device unrecoverable (P1, verified)
- **Problem:** the USB recovery console (`RESET_ADMIN_CREDENTIALS`) and BOOT recovery start only if auth initialised (`main.cpp:140-151`).
- **Required:** the USB console and BOOT service always start. If auth failed to initialise, the console offers `RESET_ADMIN_CREDENTIALS CONFIRM` and reports the auth error; BOOT setup mode opens the AP and allows a fresh enrollment after the owner confirms reset over USB. History and Wi-Fi credentials are not touched.
- **Tests:** a host-testable auth-load policy covering corrupt `bootstrap`, wrong-size `verifier1`, and a downgrade-era value; device test plan in the TODO.

### 6.2 A failed new image never rolls back on its own (P1, verified)
- **Problem:** the health gate has no timeout and never calls `esp_ota_mark_app_invalid_rollback_and_reboot`; the task watchdog doesn't panic.
- **Required:** if a pending image hasn't passed the health gate within 120 s of boot, it marks itself invalid and reboots into the previous slot. `CONFIG_ESP_TASK_WDT_PANIC=y` (with the existing tasks feeding the watchdog). Normal, offline and no-probe boots still pass the gate.
- **Tests:** host test of the gate policy with a fake clock; device test with an injected failing service (see the earlier OTA failure-injection record).

### 6.3 Failed logins stall the web server (P1, verified)
- **Problem:** each password check runs the 100k-iteration PBKDF2 (about 15 s) on the single HTTP task, under one global lockout (`auth_service.cpp:138-143, 204-218`). Repeated wrong guesses stall all REST and WebSocket traffic and lock out the owner.
- **Required:**
  - Password checks run off the HTTP task: the login handler uses the HTTP server's async request API and a worker task answers on the request copy, so other requests and WebSocket traffic continue during the derive.
  - Only one derive runs at a time; a login while one is running is refused immediately with `429` and `Retry-After`.
  - Lockout is per client address (5, 10, 20, 40, then 60 s after consecutive wrong passwords; 0.5 s spacing after a correct one), so a guessing client doesn't lock out the owner on another address. A separate global cap is not needed: with one derive in flight (about 15 s) the total rate is already bounded to about four attempts a minute.
  - A key-derivation failure (for example the worker can't start, or no entropy for the session token) does not count as a wrong password; the client waits 2 s.
  - Session access from the HTTP and worker tasks is mutex-protected and callers receive copies, not pointers into the session table.
- **Tests:** host tests of the lockout policy; device test that `/status` and the WebSocket keep responding during repeated wrong logins.

### 6.4 Auth internals (P3)
- The KDF wait uses a dedicated task-notification index, so a stray notification can't free the request early (`auth_service.cpp:143`).
- `setup_secret()` and `enrolled_` are read safely across tasks (copy under a mutex, or atomic snapshot) (`main.cpp:321,327`, `auth_service.cpp:176`).
- `run_core_tests()` result handling is covered by §4.2.
- `AuthService` lockout, backoff and enroll/rotation branches get host tests behind a small NVS/KDF seam.

### 6.5 OTA authenticity (P3)
The upload checks the image's `esp_app_desc_t.project_name` against this firmware before writing, and the docs state that signed images and secure boot are out of scope.

## 7. History storage, event log and API

### 7.1 Interrupted compaction disables the event log (P1)
`event_store.cpp:408` creates `events.bin.tmp` exclusively and `recover()` never removes a leftover. Recovery deletes or completes a stale temporary file at mount. The existing test that locks in the failing behaviour (`storage_tests.cpp:940-966`) is changed to require recovery.

### 7.2 History queries and export stall the server (P1)
- **Problem:** every `/history` request took about 10 s on the device even with only 188 KB of history, on the single HTTP task with the history mutex held and no deadline. The cost was per record, not per byte: `HistoryStore::visit` did a directory lookup and a segment lookup after every record (each a slow LittleFS call) and read each segment about four times.
- **Done (R02-05):**
  - Each segment is read once into memory (segments are 32 KiB), validated, and its records are delivered from that snapshot. A file replaced during a callback cannot splice data into the result, and the visit is still reported as failed; no per-record filesystem checks remain.
  - `HistoryService::query` has a 3 s budget; past it delivery stops and the answer is `truncated`.
  - Measured on the board: bounded history 10.3 s to 0.34-0.61 s, CSV export 9.5 s to 0.64 s, and the heavy authenticated load median 9.4 s to 0.49 s (625 requests, all 200).
- **Not done, deliberately:** a per-segment time index and releasing the mutex between segments. At the measured cost they are not needed yet; revisit if full-retention measurements (R02-09) show slow queries. CSV export at full retention (about 3 MiB of history) is still unmeasured.
- **Tests:** host tests cover snapshot delivery under a mid-visit file replacement; latency is recorded under `docs/validation/`.

### 7.3 Event flooding (P2)
`storage_error` events are limited to one per state change plus one reminder per hour while the fault persists (`history_service.cpp:201-216`). This joins the per-sensor limit in §3.7 so the journal keeps alarm history for weeks.

### 7.4 Health flags and storage info (P2)
- `event_healthy_` returns to true after writes succeed again (`history_service.cpp:64,215,225`).
- `storage_info()` keeps the last known values when its mutex wait times out, instead of reporting `storage_available:false` (`history_service.cpp:155-166`).

### 7.5 Degraded history (P2)
One corrupt completed segment no longer makes all history unavailable: it is skipped and reported (`history.cpp:317`). A transient append error is retried on the next bucket instead of disabling appends until reboot (`history.cpp:592-595`).

### 7.6 Contract and API edge cases (P2)
- `PATCH /settings` and `PATCH /sensors/{rom}` reject `{}` (`minProperties: 1`) (`http_api.cpp:542-594, 948-1027`).
- History queries reject uppercase hex and duplicate `rom_id` values, or the contract documents that they are accepted (`history_query.cpp:21,26`).
- cJSON allocation failures in history responses set `truncated` instead of silently dropping points (`http_api.cpp:706-748`).
- The receive override at `http_api.cpp:61` is removed on stop; `HistoryService::start` frees its mutex when queue creation fails (`history_service.cpp:33-46`).
- WebSocket backpressure: document that a client is closed on `EAGAIN` (T07-08's "bounded per-client queues" wording is corrected), or add a one-frame per-client slot.
  - **Chosen:** documented in `contracts/API.md` (no queue; a client that cannot take a whole frame is closed). The T07-08 wording in `docs/TODO.md` is corrected under R09-08 with owner approval.

## 8. Web application

### 8.1 The heartbeat kills reconnects (P1, verified)
`live.ts:104-109` closes the current socket after 15 s of silence even while it is still connecting, and `attempt` resets only on open, so backoff grows. The heartbeat closes only sockets that are `OPEN`; `connect()` resets the silence timer; the banner doesn't flicker during retries. A test fires `onopen` and simulates silence and reconnect.

### 8.2 Other web defects (P2)
- A failed REST resync no longer blanks healthy live values: values stay shown and marked stale (`live.ts:49-51`, `App.tsx:205,237`).
- `request()` checks the status before parsing JSON; non-JSON errors and timeouts produce readable messages and 401 still triggers sign-in (`auth.ts:40`).
- An empty display-position field produces a field error, not `order = -1` (`SensorsView.tsx:217`).
- `temperature()` requires its unit argument (`client.ts:33`).
- A failed `getSettings` shows an error and retries instead of silently using °F (`App.tsx:66`).
- Gap tooltips show time, not raw milliseconds (`HistoryView.tsx:294`); a zoomed view with no points says so instead of hiding the section (`HistoryView.tsx:251`); history re-fetches when the clock first synchronises (`HistoryView.tsx:58`).
- Sensor thresholds and calibration are entered in the configured unit (offsets and hysteresis convert by 1.8 without +32) and sent in °C, or the fields are clearly labelled °C on the page (decision D5).
- Accessibility: range chips use `aria-pressed`; error alerts have `role="alert"`; the theme choice is saved.

### 8.3 Web tests (P3)
- `App.test.tsx` routes `fetch` by URL, so `/settings` returns a valid settings object. Tests cover a saved `units: 'C'` reaching the dashboard and the default °F.
- `live.test.ts`'s fake socket fires `onopen`.
- App tests stub `WebSocket` instead of using jsdom's real one.
- The simulator returns `units: 'F'` by default and `probe-scan enabled:false` as the contract says, and serves non-empty history and alerts so `tools/test-simulator-live.mjs` validates real data.

## 9. Documentation and release

- `docs/SECURITY_DESIGN.md` is updated: 80-bit owner code, 60-bit Wi-Fi key, the public read model (§12), per-client lockout, OTA project-name check, out-of-scope items.
- `docs/USER_GUIDE.md` mentions the °F default, both units on the LCD, and the `.local` address.
- `tools/release_provenance.py` compares `web/dist` with `firmware/components/http_api/generated/web_assets.json`, reads the ESP-IDF commit from the SDK used, checks `--commit` against `git rev-parse HEAD`, refuses a dirty tree, and reads the version from the app descriptor. CI builds the web app once.
- `tools/check-secret-leaks.py` also scans staged files and recent history for the current setup values (read from the device setup file when present), and fails rather than skipping when it can't check.
- `docs/TODO.md` corrections (only with the owner's approval, per `CLAUDE.md`): reopen or annotate T02-05, T02-09, T02-10, T02-11, T02-13, T03-04, T03-05, T03-07, T04-04, T04-09, T04-10, T05-10, T06-05, T06-07, T06-10, T06-14, T09-02 and T10-06 until their fixes land; move T09-14's unrelated notes; refresh T11-09's counts.

## 10. Security model after the changes

- **Who can read:** anyone who can reach the device on the LAN or its setup AP can read current readings, history, CSV export, alert history, sensor names and probe ROM IDs, clock status and display settings.
- **Who can change:** only a signed-in owner can change sensor or device settings, Wi-Fi, firmware, reboot, factory reset or the password. CSRF, Origin and Host checks stay on every mutation.
- **Never public:** Wi-Fi SSID and IP details (`/wifi/status`), Wi-Fi scans, the setup Wi-Fi key and owner code (LCD and USB only), session tokens.
- **Accepted limitation:** traffic is plain HTTP on the LAN, as before. Anyone on the network can read the public data; that is the owner's choice.
- **Abuse protection:** public endpoints are rate-limited per client address (§12.4) so they can't starve the owner or the sensor loop.

## 11. Design-quality items (P3, no behaviour change)

Listed so nothing from the review is lost: magic layout coordinates and repeated "compact" checks (§5.6), `provisioning_service.cpp` being mostly a USB test bridge (rename or split into `usb_bridge`), file-identity checks compiled only on the host (document or simplify), history query cost (§7.2), and the hard-coded firmware version in provenance (§9).

## 12. Feature: public read-only access

### 12.1 Route policy

| Route | Method | Access after the change |
| --- | --- | --- |
| `/api/v1/status`, `WS /api/v1/live` | GET | Public |
| `/api/v1/history`, `/api/v1/history/export` | GET | Public (rate-limited) |
| `/api/v1/alerts` | GET | Public |
| `/api/v1/sensors`, `/api/v1/sensors/storage`, `/api/v1/probe-scan` | GET | Public (read-only fields) |
| `/api/v1/clock` | GET | Public |
| `/api/v1/settings` | GET | Public, display fields only (units, timezone, display mode, dwell, lookback, hostname); NTP server is owner-only (decision D3) |
| `/api/v1/auth/state`, `/auth/login`, `/auth/enroll` | as today | Public |
| `/api/v1/settings`, `/api/v1/sensors/{rom}` | PATCH, DELETE | Owner |
| `/api/v1/wifi/*`, `/api/v1/ota`, `/api/v1/system/*`, `/api/v1/auth/password`, `/api/v1/auth/logout` | all | Owner |

Unknown routes stay `404`; owner routes without a session stay `401`. Public `GET`s still require a valid `Host`. A public WebSocket upgrade still requires a matching `Origin` (it keeps cross-site pages from reading the stream); public `GET`s accept a missing `Origin`.

### 12.2 Contract
- `contracts/API.md` gains an "Access" column. The public `GET /settings` response either omits owner-only fields (a new `device-settings-public` schema) or the owner response adds them; the version decision is recorded (D3).
- `auth/state` reports `{ enrolled, signed_in, csrf? }` so the web knows whether to show owner controls.

### 12.3 Web
- The app no longer wraps everything in `AuthGate`. Dashboard, History and Alerts open without signing in.
- Sensors and Settings show a "Sign in to change settings" prompt (inline `AuthGate`) and the editable forms after sign-in. A small sign-in/out control sits in the app bar.
- A 401 from an owner route opens sign-in instead of a dead page.

### 12.4 Load protection
- At most 4 WebSocket clients (three viewers; owner sessions keep a reserved slot) with the server's open-socket limit at 6. The first draft said 8; 6 clients with 8 sockets was tried and measured on the board (free internal memory fell to 15 KB under load, against 29.6 KB with 4 clients), so the figure stays at 4 until memory is made available, up to 2 concurrent history queries, and per-client limits: 1 history query per second and 1 CSV export per 30 s; excess returns `429` with `Retry-After`.
- Public requests never block the sensor loop or the display task (§7.2).

### 12.5 Tests and acceptance
- Host/API tests: every route in §12.1 checked anonymous and signed-in; mutations without session or CSRF rejected; public settings omit owner-only fields; rate limits return `429`.
- Web tests: dashboard and history render without sign-in; settings require sign-in.
- Device test: a phone with no session sees readings and history; changing a threshold asks for the password.

## 13. Feature: dependable mDNS

Today the firmware registers `temperature-monitor.local` and an `_http._tcp` service (`wifi_service.cpp:63-81`), and `Host` checks accept that name.

### 13.1 Behaviour
- **Name:** a `hostname` setting, default `temperature-monitor`, validated as one RFC 1123 label (1-63 characters, `a-z`, `0-9`, `-`, not starting or ending with `-`). Changing it re-registers mDNS without a reboot and updates the `Host` allow-list; old sessions keep working.
- **Conflicts:** if another device already uses the name, the active name (for example `temperature-monitor-2`) is read back from the mDNS stack, reported in status, and shown in the web Settings and on the LCD setup screen. The default could also include the MAC suffix (decision D4).
- **Service:** advertise `_http._tcp` on port 80 with TXT `path=/` and `model=temperature-monitor`, so network browsers list it.
- **Visibility:** the web Settings page shows `http://<name>.local`; the LCD setup screen adds it as an alternative to `192.168.4.1` once the station is connected; `GET /status` includes `mdns_hostname`.
- **Failure:** if mDNS can't start, the device keeps working by IP and reports `mdns: unavailable`.

### 13.2 Storage and contract
- `hostname` is added to device settings. The settings record moves from `TMD1` to `TMD2`; `TMD1` records still load with the default hostname (migration tested).
- `device-settings` schemas gain `hostname`; the schema version change is recorded.

### 13.3 Tests and acceptance
- Host tests: hostname validation, TMD1→TMD2 migration, Host allow-list follows the active name.
- Device tests: resolve and open `http://temperature-monitor.local` from Linux, Windows, macOS/iOS and Android (note that Android before 12 and Linux without Avahi/nss-mdns may not resolve `.local`); rename and re-resolve without reboot; two devices on one network get distinct names.

## 14. Owner decisions

Confirmed by the owner on 2026-11-05: D1 to D7 are all accepted as proposed below.

| ID | Decision | Decided |
| --- | --- | --- |
| D1 | Is CSV export public, or owner-only? | Public, rate-limited |
| D2 | Are alert history and sensor ROM IDs public? | Yes |
| D3 | Public `GET /settings`: split schema, or owner-only fields omitted from one response? | Separate public schema; NTP server owner-only |
| D4 | Default mDNS name: plain `temperature-monitor`, or with the MAC suffix (`temperature-monitor-a87739`) to avoid clashes? | Plain name, with conflict handling |
| D5 | Sensor thresholds in the web: entered in the configured unit, or kept in °C? | Configured unit, stored in °C |
| D6 | Unprovisioned device: alternate setup and Overview on the LCD, or setup only? | Alternate |
| D7 | Owner-code characters outside the alphabet (`0`, `1`, `I`, `O`): reject with a message, or map to look-alikes? | Reject with a message |
| D8 | Sensor calibration: ice point only, or a second point (boiling water, a reference thermometer)? Decided by the owner on 2026-11-12 (§17). | Ice point only |
| D9 | Web typeface: system font, or Roboto Flex built into the image? Decided 2026-11-12 (§18). | Roboto Flex, built in |
| D10 | Web colour scheme. Owner asked for a palette to be chosen; decided 2026-11-12 (§18.2). | Material 3 tonal-spot scheme from seed `#2F6B4F` (evergreen) |
| D11 | Events: its own page, or a tab of Alerts? Decided 2026-11-12 (§18.6). | A tab of Alerts; Alerts is the first tab and the default |

## 15. Verification

- Each change: the focused host or web test first (failing before the fix where it's a defect), then `./tools/verify.sh`; firmware changes also `./tools/verify.sh --firmware`.
- Board checks are recorded under `docs/validation/` and labelled host, simulated or physical. Builds and simulations don't count as hardware acceptance.
- No `erase-flash` or partition change. History and NVS survive every flash.

## 16. Follow-ups from the owner-code removal and the code review

These close the open items in `REVIEW_FIX_TODO.md` Phase R10. Each subsection states the behaviour
wanted and the acceptance test.

### 16.1 History query speed and the rate limit (R10-03)

- Behaviour: a history query reads only the storage it needs for its time range, so it finishes
  well under one second on a full store. Today every query scans the whole store and filters
  afterwards, which takes about 1.2 s even for one bucket.
- Why it matters: the public history limit is one query per second per client. A query longer than
  a second means a sequential client is never refused, so the limit does nothing.
- Acceptance: a host test shows a bounded scan visits only the segments in range; the device test's
  history rate-limit check passes unchanged on the board.

### 16.2 Device test sensor count (R10-04)

- Behaviour: `device_api_test.py` takes the expected probe count (`--expect-sensors`) and checks
  for exactly that count. A bare board reports the probes saved on it, so the count is set per board
  rather than assumed.
- Acceptance: the test fails when the count differs.

### 16.3 Stale owner code (R10-05)

- Behaviour: at startup the firmware erases the `bootstrap` key once, so no owner code remains in
  storage. The erase is idempotent and does not touch the administrator verifier.
- Acceptance: host test: after startup the key is absent, and the verifier is unchanged. The
  migration test that asserts the old key is ignored is removed. On the board the key is absent
  after one reboot.

### 16.4 Enrollment refusal messages (R10-06)

- Behaviour: the enrollment refusal names its cause:
  - setup window closed: "Hold BOOT for five seconds to open setup, then try again";
  - already enrolled: "An administrator password already exists";
  - spacing after an attempt: "Wait five seconds and try again".
- Acceptance: one test per message in the API and the sign-in form.

### 16.5 History retention measurements (R10-08)

- Behaviour: record the bytes per history record and the retention for one to five probes, and
  the oldest and newest timestamps the store holds, in `docs/HISTORY_FORMAT.md`.
- Acceptance: the numbers are in the document, with the measurement method.

### 16.6 Firmware validity and rollback (R10-09)

- Behaviour: a new image is marked valid only after startup health checks pass (storage mounted,
  sensors read, HTTP answering). If they fail, the device rolls back to the previous image. The
  update status shows on the LCD and in the web UI.
- Acceptance: host tests for the health gate; on the board, the web UI shows the status.

### 16.7 Resource profile (R10-10)

- Behaviour: profile heap, client counts, LVGL memory, flash writes and Wi-Fi traffic over a soak
  run.
- Acceptance: a recorded profile with no growth after warm-up.

### 16.8 Documentation reconciliation (R10-11)

- Behaviour: `SPEC.md`, `TODO.md`, the wiring document, the UI specs and the diagrams agree with the
  final pinout and measurements.
- Acceptance: a reviewer finds no conflicting statements.

### 16.9 Release (R10-12)

- Behaviour: versioned firmware binaries, web build provenance, checksums and release notes with
  known issues. Published only after §16.1 to §16.8 are complete.

### 16.10 Approval-gated edit (R10-13)

- `docs/TODO.md` is edited only with the owner's approval, as R09-08 requires.

### 16.11 Login reliability (R10-14)

- Behaviour: a correct password always signs in when the device is otherwise healthy. The password
  check must not depend on finding a large free block of internal RAM, and a login the device
  could not check is reported as a device problem (`503`, retry shortly), never as a wrong
  password.
- Acceptance: host tests for the failure causes; on the board, repeated logins in the first minutes
  after boot all succeed.

### 16.12 Internal RAM headroom (R10-15)

- Behaviour: long-lived bulk data (the LCD trend history and its restore) lives in PSRAM, so
  internal RAM stays free for Wi-Fi, the network stack and task stacks. HTTP responses are not
  dropped for lack of memory.
- Acceptance: on the board the internal-RAM low-water mark stays well above the size of a network
  buffer after boot and trend restore, and `device_api_test.py` passes.

## 17. Sensor calibration in an ice bath

Owner request (2026-11-12). The monitor watches a grow room, about 18 to 30 °C. Calibration stays
a single offset per probe (`calibration_c`, ±12.7 °C, also kept in the probe's own memory). This
section adds a guided way to measure that offset in an ice bath, a pause of that probe's
temperature alerts while it is in the bath, and instructions for doing it well.

Calibration changes only `calibration_c` (and, through it, the copy in the probe's memory). Names,
order, enable state, limits, hysteresis, persistence and the disconnect alert are not changed.
Limits stay absolute temperatures compared with the corrected reading, and history already stored
keeps the offset in force when it was recorded.

### 17.1 Why ice only (D8)

- A stirred bath of crushed ice and a little water sits at 0.0 °C within about ±0.1 °C anywhere,
  with no equipment.
- A DS18B20's error is almost all a fixed offset: its rated error is ±0.5 °C from −10 to +85 °C,
  and it changes by only hundredths of a degree between 0 and 30 °C. An offset set at 0 °C
  therefore holds across a grow room's range.
- Boiling water is not used. Its temperature falls about 1 °C per 300 m of altitude and moves a
  few tenths with the weather, and most waterproof probes have PVC cable and heat-shrink rated
  near 80 to 105 °C, which repeated boiling can damage.
- Out of scope: a second calibration point and a gain (slope) correction, a reference-thermometer
  check, and humidity (the DS18B20 cannot measure it).

### 17.2 Guided calibration on the Sensors page

- Each sensor's tab has a **Calibrate in ice** button, shown to the signed-in owner while the
  probe is connected and sensor storage is writable.
- The flow:
  1. Instructions: make the ice bath (§17.4), put the probe in it, wait. Starting the flow starts
     the probe's calibration hold (§17.3).
  2. Measuring: the page shows the live reading and collects the probe's readings from the live
     feed, each sample once. The uncalibrated reading is the live (calibrated) reading minus the
     probe's current `calibration_c`.
  3. Steady: the reading counts as steady when the last 12 readings (one minute at the 5-second
     cycle) span no more than 0.125 °C, two steps of the probe's 0.0625 °C resolution. The page
     says it is waiting while they don't, and gives up after 10 minutes with advice (more ice,
     stir, check the probe is fully covered).
  4. Result: the new offset is 0 minus the mean of those 12 uncalibrated readings, rounded to
     0.1 °C, the step the probe's memory holds, so the monitor and the probe's copy agree exactly.
     The page shows the current and new offsets and how much every reading will change, and asks
     before saving.
     The confirm screen also shows the probe's current high and low limits, with the note: "Your
     high and low limits are not changed. If you set them to allow for this probe reading high or
     low, adjust them after saving." The flow offers a link to those fields on the same tab; it
     never changes them itself.
  5. Saving uses the existing `PATCH /sensors/{rom_id}` with the sensor's current ETag; nothing
     else in the sensor's settings changes. Afterwards the page says the probe should now read
     0.0 °C in the bath, and asks for the probe to be dried and put back.
  6. **Done** (or **Cancel** at any step) shortens the hold so it ends five minutes later, while
     the probe warms back up to room temperature.
- Guards:
  - The probe disconnects, the sensor bus faults, or the sensor's revision or offset changes during
    measuring: measuring stops and must be started again.
  - A result beyond ±12.7 °C is refused: that is a faulty probe or a bad bath, not an offset.
  - A result beyond ±2 °C (four times the probe's rated error) is offered only with a warning to
    check the bath and the probe first.
  - Cancel at any step leaves the settings as they were.
- The readings and offsets are shown in the unit chosen by the page's °C/°F switch; the bath is
  still 0 °C (32 °F) and the saved offset is in °C.

### 17.3 Alert pause during calibration (the calibration hold)

A probe in an ice bath reads about 0 °C, which would raise a false low alert in a grow room and
record the bath in that probe's history. The hold stops both, for that probe only.

- `PUT /sensors/{rom_id}/calibration-hold` with `{"minutes": 1..20}` starts or renews the hold to
  end that many minutes from now; `DELETE` ends it. Owner session and CSRF, like other sensor
  changes, but no `If-Match`: the hold is not a setting. Refused (`409`) for a probe that is not
  connected. At most one hold per probe; renewing replaces the end time.
- The monitor keeps the hold in RAM only. It ends at its end time, on `DELETE`, on retirement, or
  at restart; it is never saved, so a closed browser or a crash cannot leave alerts paused for
  longer than 20 minutes. The web flow asks for 15 minutes and renews every 5 minutes while it is
  open, and asks for 5 minutes at Done or Cancel.
- While a probe is held:
  - its high and low conditions are not raised, and an active high or low alert for it clears;
  - its readings are not added to history, so the chart shows a gap there rather than the bath;
  - the disconnect alert and sensor-bus fault reporting still work: a real unplug still matters;
  - `GET /sensors` and the live feed report `calibrating: true` with `calibration_hold_ends_uptime_ms`,
    and the web shows "Calibrating" for the probe instead of an alarm state. The LCD shows `CAL`
    in the probe's state position instead of `HIGH` or `LOW`.
- When the hold ends, alert checks resume with the normal hysteresis and persistence, so a probe
  still warming up raises a low alert only if it stays low for its persistence time.
- The event log records `calibration` events, `started` and `ended` (with the reason: finished,
  expired, cancelled, disconnected, retired), so a gap in history has an explanation.
- Contract decision: additive within schema v1, like `probe_calibration`: the new route, the two
  read-only fields (refused by `PATCH`), and the `calibration` event kind. The web app ships in
  the same image. Storage: no settings change; the event journal gains one event kind, and an
  older firmware reached by rollback must skip an event kind it does not know rather than fail the
  journal (checked before release).

### 17.4 User guide

`docs/USER_GUIDE.md` gains a calibration section:

- The ice bath: a tall container packed with crushed ice (not cubes), topped up with cold water
  just to below the ice surface, stirred, left a minute; the probe's metal tip at least 5 cm in,
  in the middle, not touching the container; stir while measuring; add ice if it floats free.
- Calibrate each probe on its own cable as it will be installed. Dry it afterwards.
- That calibration changes only the offset: check the high and low limits afterwards if they were
  set to allow for a known error; alerts for that probe pause while it is in the bath.
- Probe placement in a grow room, which matters more than calibration: shade the probe from the
  grow lights (a vented white shield or a white tube open at both ends), mount it at canopy
  height, away from heaters, fan outlets, walls and the lamp's hot spot, and where air moves past
  it.

### 17.5 Acceptance

- Host tests (firmware): a held probe raises no high or low condition and clears an active one;
  its readings are not aggregated into history; disconnect and bus-fault alerts still occur; the
  hold ends at its end time, on `DELETE`, on retirement and at restart; renewal replaces the end
  time and the 20-minute cap holds; after the hold, persistence applies; the `started`/`ended`
  events with their reasons; the route's auth, CSRF, `409` and range checks.
- Web tests: the button's visibility rules; the steadiness rule (12 readings within 0.125 °C, a
  repeated sample counted once); the offset arithmetic with a non-zero current offset and its
  rounding; each guard; cancel; that the save sends only `calibration_c` with the current ETag;
  the °F display; the limits reminder on the confirm screen with the probe's current limits; the
  hold started, renewed and shortened at Done and Cancel; "Calibrating" shown for a held probe.
- Display test: `CAL` in a held probe's state position.
- On the board, with a real probe in a real ice bath: the flow completes, the probe then reads
  0.0 ± 0.1 °C in the bath, the offset is saved on the probe (`stored`), and it survives a restart.
  This is also the first board check of a non-zero offset written to a probe. During the bath no
  low alert is raised for that probe (with its low limit above 0 °C), its history shows a gap,
  the LCD shows `CAL`, and the event log shows the hold starting and ending.

## 18. Material 3 redesign of the web app

Owner request (2026-11-12): use Material Design 3 properly, and make the web app look slicker,
more modern and professional. Today the app uses MUI 9 (`@mui/material`), whose components follow
Material Design 2, with a few overrides that resemble Material 3 (pill buttons, no capitals, 20 px
corners, flat outlined cards, a hand-picked green). It has no Material 3 colour roles, type scale,
component styles or icons, and the navigation is a custom text-only sidebar. This section replaces
that with a real Material 3 layer on MUI and redesigns the pages. Behaviour, routes, the API and
the LCD do not change except where stated.

### 18.1 Approach

- Keep MUI and React. Material 3 comes from a generated theme and component style overrides, not
  a new component library: Google's `@material/web` is in maintenance mode and not React, and no
  maintained React library implements Material 3 fully.
- New dependencies are pinned to exact versions like the others: `@material/material-color-utilities`
  (dev only, to generate tokens), `@mui/icons-material` (one import per icon so only used icons are
  built in), and the Roboto Flex font files.

### 18.2 Foundation

**Colour (D10).** A Material 3 tonal-spot scheme generated from seed `#2F6B4F`, a calm evergreen
that suits a grow room. The generator script lives in `web/scripts/`, writes a committed token
file, and a test checks that the file matches the script's output. Key roles:

| Role | Light | Dark |
| --- | --- | --- |
| primary / on-primary | `#236A4C` / `#FFFFFF` | `#8FD5B0` / `#003824` |
| primary-container / on- | `#AAF2CB` / `#005235` | `#005235` / `#AAF2CB` |
| secondary-container / on- | `#D0E8D8` / `#364B3F` | `#364B3F` / `#D0E8D8` |
| tertiary / tertiary-container | `#3C6472` / `#C0E9FA` | `#A4CDDE` / `#234C5A` |
| error / error-container | `#BA1A1A` / `#FFDAD6` | `#FFB4AB` / `#93000A` |
| surface / on-surface | `#F5FBF4` / `#171D19` | `#0F1511` / `#DEE4DE` |
| surface-container low / mid / high | `#F0F5EF` / `#EAEFE9` / `#E4EAE3` | `#171D19` / `#1B211D` / `#262B27` |
| on-surface-variant / outline / outline-variant | `#404943` / `#707973` / `#C0C9C1` | `#C0C9C1` / `#8A938C` / `#404943` |

Status colours are Material 3 custom colours harmonised to the seed, each with a container pair:

| Status | Light (colour / container) | Dark (colour / container) |
| --- | --- | --- |
| High (too warm) | `#9B4600` / `#FFDBC9` | `#FFB68D` / `#763300` |
| Low (too cold) | `#006491` / `#C9E6FF` | `#8ACEFF` / `#004C6E` |
| Warning (stale, no time, Wi-Fi lost, simulation) | `#775A00` / `#FFDF99` | `#F4BF2A` / `#5A4300` |
| Fault (disconnected, bus fault, storage) | the error roles | the error roles |
| Normal | primary-container roles | primary-container roles |

Chart series, from the colour-blind-safe Okabe–Ito hues, at tone 45 (light) and 75 (dark), in
probe display order: `#006FAE`/`#77BFFF`, `#8F6200`/`#F4AB18`, `#007A58`/`#51CE9F`,
`#A24D7F`/`#FC9AD0`, `#B04D00`/`#FFA26F`. A probe keeps its colour on every page.

The theme follows the system light/dark setting, with the existing override kept.

**Type (D9).** Roboto Flex, built into the image and served by the device, so it works with no
internet. Only the weight and optical-size axes are kept and the Latin set plus `°`, `±`, `−`,
`…`, `·` and `–`; the WOFF2 file is at most 80 KB. `font-display: swap` with the system font as
fallback. The licence (SIL OFL 1.1) is shipped beside it, and `docs/FONTS.md` gains a web section
with the source, version, checksum and the subsetting command. The Material 3 type scale is used
(display, headline, title, body and label, each large/medium/small). Temperatures use tabular
figures so digits do not shift as values change.

**Shape, spacing, elevation, motion.** The Material 3 shape scale (4, 8, 12, 16, 28 px and full),
an 8 px spacing grid (4 px for fine adjustments), surfaces told apart by container tone rather
than shadows, and Material 3 standard easing and durations for state changes, turned off when the
system asks for reduced motion.

**Icons.** Material icons (outlined set), each with a text label or an accessible name.

**Components.** Styled to Material 3: filled, tonal, outlined and text buttons; icon buttons;
segmented buttons (history range, °C/°F); primary and secondary tabs; switches; filter, assist
and input chips; outlined text fields; filled and outlined cards; dialogs; snackbars; menus;
progress indicators.

### 18.3 Navigation and top bar

- At 840 px and wider, a navigation rail on the left: the app's mark and name, then five
  destinations, each an icon with a label and the Material 3 active indicator (pill behind the
  icon).
- Below 840 px, a navigation bar at the bottom with the same five destinations, icons with
  labels. The page uses the full width; no horizontal scrolling at 360 px.
- Destinations: Dashboard, History, Sensors, Alerts, Settings. Alerts shows a badge with the
  number of active alerts.
- A compact top app bar: the page title, the device's network name, and icon buttons for the
  theme (light, dark, system) and signing in or out, with tooltips. The repeated "Local
  temperature monitor" label goes.
- Device status (Wi-Fi, time sync, history logging, simulation) is shown as assist chips with an
  icon and the status colours of §18.2, only for conditions that need attention; when everything
  is fine a single "All systems normal" chip shows instead.

### 18.4 Dashboard

- One card per enabled probe, in display order, in a responsive grid (one column on a phone, up
  to five across on a wide screen). Each card shows:
  - the probe's name, and its series colour as a small mark;
  - the temperature, large, in tabular figures, with its unit;
  - the state as a coloured chip with an icon (normal, high, low, disconnected, stale), using the
    status colours;
  - a 24-hour sparkline;
  - today's minimum and maximum, and the low and high limits as a small range bar with the current
    value marked;
  - "Updated n s ago" in small text, highlighted when stale.
- The ROM ID is not shown on the Dashboard.
- Below the cards, a 24-hour chart of all probes (the History chart component, §18.5) with a link
  to History. It uses one history query for all probes, refreshed every five minutes, within the
  one-query-per-second limit; live readings extend it between refreshes.
- While the first data loads, skeleton cards show in place of "Connecting…".

### 18.5 History

- A toolbar: the range as a segmented button (1 h, 24 h, 7 d, 30 d); zoom in, zoom out, earlier,
  later and reset as icon buttons with tooltips, hidden rather than greyed when they do nothing;
  refresh as an icon button; export in an overflow menu.
- Probe selection as filter chips in the series colours; the ROM ID moves to the chip's tooltip.
- The chart: a time axis with readable ticks (local time, or "elapsed since boot" when the clock
  is not synchronised), temperature ticks at round values, light gridlines, smooth lines without a
  dot on every point, gaps shaded, and hover or tap to show each probe's value at that time.
- Each probe's minimum, maximum and average for the range in a compact table below the chart.
- Notices (simulated history, unsynchronised clock) as one compact info line, not a large box.

### 18.6 Alerts page with Alerts and Events tabs (D11)

- The Alerts page has two tabs, **Alerts** first and selected by default, then **Events**.
- `/alerts` opens the Alerts tab and `/events` opens the Events tab, so existing links and
  bookmarks still work; switching tabs updates the address.
- Alerts are listed with a status icon and colour, the probe or system part affected, the value
  and limit where it applies, and how long ago it began. An empty list shows "No active alerts"
  with an icon.
- Events keep their current content and paging, restyled as a list with icons by kind.

### 18.7 Sensors, Settings and sign-in

- The existing tabs use Material 3 tabs; forms use outlined text fields with supporting text and
  the °C/°F switch becomes a segmented button.
- Actions follow one rule: the main action of a form is a filled button, secondary actions are
  tonal or text buttons, and destructive actions (retire a probe, factory reset, restart) are
  error-coloured and confirm in a dialog.
- Save results show in a snackbar; field errors show under the field.
- The sign-in form is a centred card with the app's mark.

### 18.8 Quality bar

- Text and essential icons meet WCAG 2.1 AA contrast in both themes (checked by a test over the
  token pairs used), keyboard focus is always visible, and every control has an accessible name.
- Layouts work at 360, 390, 768, 1280 and 1600 px with no horizontal scrolling.
- Loading shows skeletons; empty states have an icon and a sentence; errors say what to do.
- Size: the compressed web build grows by at most 150 KB, the font included, and the firmware
  still fits both 4 MiB OTA slots. Measured before and after.

### 18.9 Acceptance

- Unit tests: the token file matches the generator; the contrast check; the navigation (five
  destinations, rail or bar by width, the Alerts badge); `/alerts` and `/events` opening their tabs
  with Alerts as default; the Dashboard card contents, without ROM IDs; the history toolbar, filter
  chips and summary table; the existing behaviour tests updated, none dropped.
- Screenshots of every page in light and dark at 390 and 1280 px, before and after, recorded under
  `docs/validation/`, checked against the simulator.
- On the board: the pages load from the device with no internet, the font is served from the
  device, the measured size is recorded, and a phone shows the navigation bar correctly.
- `docs/WEB_UI_DESIGN_SPEC.md` and `docs/USER_GUIDE.md` describe the new layout.
