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
- At most 8 WebSocket clients (owner sessions keep a reserved slot), up to 2 concurrent history queries, and per-client limits: 1 history query per second and 1 CSV export per 30 s; excess returns `429` with `Retry-After`.
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

## 15. Verification

- Each change: the focused host or web test first (failing before the fix where it's a defect), then `./tools/verify.sh`; firmware changes also `./tools/verify.sh --firmware`.
- Board checks are recorded under `docs/validation/` and labelled host, simulated or physical. Builds and simulations don't count as hardware acceptance.
- No `erase-flash` or partition change. History and NVS survive every flash.
