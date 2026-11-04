# Code Review and TODO Audit — 2026-10-30

Reviewed revision: `290640cfe801cffe7829103630458039bf7cf8f2` on `master`. The working tree was clean when review began. Scope: all 145 numbered tasks in `docs/TODO.md`, its nested follow-up work, phase gates, definition of done and optional backlog; source, configuration, tests and supporting requirements. The implementation and TODO checkboxes were not changed. This report is the only new repository document.

The review found software defects and verification gaps despite passing automated checks. The previous conclusion that all remaining work required peripherals was too strong. This is targeted source inspection and requirement tracing, not proof that every possible defect has been found. No new flashing, network reconfiguration, credential mutation, destructive storage check or physical hardware test was performed during this review.

## Findings

All findings below are P2 (normal priority) except F7, which is P3 (low priority). No confirmed P0/P1 issue was found. F1 and F3 have fresh native reproductions; other findings are established by source inspection, with runtime limits stated explicitly.

### F1 — Five saved ROM identities prevent normal replacement of a probe

Location: [sensor_registry.cpp:15](../firmware/components/sensor_registry/sensor_registry.cpp#L15). Related: T02-10, T03-03, T10-07.

After five identities have been discovered and persisted, removing one and installing a different probe leaves the old identity managed and the new one rejected. Disabling the old probe does not free its registry entry; neither the HTTP API nor the web editor offers a forget/replacement operation. Reboot reloads the same five entries. This makes a routine replacement unusable through the normal interface without resetting sensor settings or editing persistent storage outside that interface. The five-managed-ROM limit is intentional; the missing safe way to retire a managed ROM is the gap.

Fresh native reproduction filled five slots, replaced one physical identity in the readings, and ran four sample cycles: `replacement_registered=0 saved_slots=5 excess=1`. The existing replacement regression starts with only one managed ROM, so it does not exercise capacity. Provide an explicit authenticated retire/replace operation that preserves old historical ROM attribution and never silently transfers labels or thresholds to unrelated hardware. Add a capacity replacement regression.

### F2 — The physical driver silently discards excess probes before reporting

Location: [onewire_service.cpp:51](../firmware/components/onewire_service/onewire_service.cpp#L51). Related: T02-10, T02-13.

Discovery sorts valid ROMs and resizes the list to five before registry sampling. Consequently the registry's excess-device counter cannot see a sixth physical device. The snapshot also never exposes that counter. A sixth device with a lower sorted ROM can displace a previously managed device from the driver's read list while being rejected by the already-full registry itself. A configured probe can then appear lost despite still being connected, with no excess-device explanation.

Preserve a bounded discovered count/overflow diagnostic, report it through the device status, and choose read bindings in a way that does not evict managed connected probes based solely on sort order. The registry-only sixth-probe test does not exercise this physical-driver truncation. This is a source-confirmed integration issue; no six-probe hardware experiment was performed.

### F3 — Downsampling removes exact gap intervals inside a time group

Location: [history_query.cpp:127](../firmware/components/monitor_core/history_query.cpp#L127); response generation at [http_api.cpp:593](../firmware/components/http_api/http_api.cpp#L593). Related: T06-12, T07-05, T09-05.

When two buckets for the same ROM and boot fall in one downsampling group, the reducer merges across a missing bucket, marks the result partial, and extends its end time. `history_gaps()` runs only on those merged points, so it cannot recover a missing interval inside one merged point. This loses the explicit intervals promised by T07-05 and makes a real outage indistinguishable from other partial-data causes. The frontend avoids connecting partial points, but that does not restore the missing time boundaries.

Fresh reproduction: buckets at 0 and 600,000 ms, query 0..900,000 with `max_points=1`, produce `downsampled_points=1 span=0..900000 partial=1 explicit_gaps=0`. The actual missing interval is 300,000..600,000 ms. Track bounded gap intervals from the original bucket stream before reduction, or split groups at discontinuities while retaining an explicit result cap/truncation policy. Test an internal gap at 7-day/30-day downsampling widths.

### F4 — Sensor edits appear applied when persistent sensor settings cannot be loaded

Location: [main.cpp:193](../firmware/main/main.cpp#L193); acceptance at [http_api.cpp:845](../firmware/components/http_api/http_api.cpp#L845). Related: T03-02, T03-10, T07-03, T09-08.

When `settings.load()` returns a corruption/schema/NVS error other than NOT_FOUND, main correctly preserves the original blob by disabling saves. It still discovers probes and accepts queued sensor edits through the API. The new settings/revision are published from RAM, and the browser reports “Settings applied.” They disappear on reboot because the save branch is permanently skipped for this boot. The API exposes no sensor-settings writable/health flag, unlike the device-preferences service.

Propagate sensor persistence availability to API/UI and reject durable edits with a clear unavailable/recovery response while the original blob is preserved, or explicitly communicate and distinguish volatile-only changes. Also expose later save failures rather than treating a published RAM revision as persistence success. This path was inspected statically; a live NVS corruption experiment was deliberately not performed.

### F5 — Web history and event times ignore the configured device timezone

Location: [HistoryView.tsx:299](../web/src/features/HistoryView.tsx#L299), [AlertsView.tsx:20](../web/src/features/AlertsView.tsx#L20). Related: T09-05, T09-10, T09-11.

Both components call `toLocaleString()` without a `timeZone` option. App fetches device settings but retains only `units` for its children. A device configured for Los Angeles viewed from a browser configured for Tokyo shows Tokyo timestamps while the LCD uses Los Angeles. The History UI explicitly says browser timezone, but T09-05 and the system specification require the user-selected timezone to govern local presentation. The user guide reflects the current browser behavior and therefore also needs reconciliation with the requirement.

Pass the selected IANA timezone into history/events and format with `Intl.DateTimeFormat`/`toLocaleString` using that zone. Alternatively change the approved requirement explicitly if browser-local presentation is intended. Test deliberately differing browser/device zones and a DST boundary. No host timezone configuration was changed in this review.

### F6 — Slowly delivered unauthenticated JSON can monopolize the HTTP task

Location: [http_api.cpp:59](../firmware/components/http_api/http_api.cpp#L59). Related: T07-01, T08-08, T10-05.

The JSON receive loop bounds total bytes to 2,048 but has no overall deadline. The server's three-second receive timeout applies to individual socket receives. A client can POST to the unauthenticated login route, advertise a bounded body, and send a byte every two seconds. Each receive can succeed, keeping the single HTTP server task inside this handler for roughly an hour for a near-limit body. Other HTTP handlers and queued WebSocket broadcasts cannot run normally during that period. Acquisition is a separate task, so this finding concerns network-service availability rather than demonstrated sensor starvation.

Use an overall monotonic body deadline and close incomplete requests, with receive waits capped by remaining time; consider bounded asynchronous handling where appropriate. Verify with a real slow HTTP body and a concurrent healthy request. The existing stalled-WebSocket regression tests a different path. This is a static control-flow finding; the potentially disruptive experiment was not run against the board.

### F7 — Contributor instructions describe a repository that no longer exists

Location: [AGENTS.md:5](../AGENTS.md#L5). Related: T11-01 and contributor onboarding.

The guide says there is no application code, build configuration, tests or formatting setup and calls `firmware/` and `web/` planned directories. It also says Git contains only an initial commit. These statements contradict the current repository and direct future contributors away from the existing commands and conventions. Update the guide to describe current modules, verification scripts and configuration. It was intentionally left untouched during this review.

## Strengths and weaknesses

The pure C++ registry, alert FSM, aggregator, codecs and query logic are separated from ESP-IDF adapters and can be exercised quickly on a host. The shared ROM identity model, Celsius units, explicit invalid values and boot-relative timestamps prevent several common telemetry mistakes. Sensor models, LCD views and the API derive from the same registry rather than independent state machines.

Storage handling is unusually defensive: CRC/version checks, preserved unknown headers, bounded records/segments, write latches after failure, durable-before-eviction ordering, and interrupted-write regressions. The WebSocket sender has a fixed send budget, explicit failed-stream quarantine and real POSIX-socket tests. Exact SDK/component/web pins and downloaded-artifact checksum verification help make builds and evidence traceable. HTTP confidentiality limits and missing physical acceptance are candidly documented.

The main weaknesses are integration/error-path coverage and maintainability. The HTTP dispatcher combines validation, auth, configuration, history and asset service in one 937-line file; the provisioning module combines button handling, recovery and sizable USB test bridges. Native policy tests and fresh fixtures leave gaps where real adapters interact, as F2/F4/F6 show. TODO entries contain long chronological logs and even a literal `\n` joining two follow-ups, making current requirements harder to distinguish from historical evidence. Additional lower-severity UI gaps include a dashboard history card that always says “No history available yet,” an initial auth-state fetch failure with no in-page retry, and BOOT progress rendered only at roughly five-second acquisition cadence. These observations are not additional independently reproduced findings.

## Verification and evidence index

Fresh `./tools/verify.sh` completed with exit 0: 19 native CTests, 14 sensor-report Python checks, 14 provenance tests, simulator REST/WebSocket integration, one headless LVGL CTest, C++/web formatting, ESLint, 56 frontend tests in 16 files, strict TypeScript and Vite production build. Log: `.build/code-review-verification.log` (local ignored scratch file). No new standalone firmware compilation was run in this review. Existing exact-commit [CI run 41038280985](https://github.com/ekkus93/esp32-lcd-temperature-sensor/actions/runs/41038280985) was independently queried and is successful for the reviewed SHA, including pinned ESP-IDF build/provenance.

Two fresh scratch reproductions link production `.build/host/libmonitor_core.a`: `.build/code-review/reproduce.cpp`, executable `reproduce`, results `reproduction-results.txt`. They demonstrate F1/F3 without altering production source or registering new project tests. There was no physical validation during the review. Earlier sanitized files under `docs/validation/` were read as historical evidence, not treated as newly repeated experiments.

Evidence keys used in the audit:

- **MAIN:** [main.cpp](../firmware/main/main.cpp): initialization, queues, acquisition, persistence, publishing and health gate.
- **BUILD:** [ci.yml](../.github/workflows/ci.yml), [check-sdk.sh](../tools/check-sdk.sh), [manifest](../firmware/main/idf_component.yml), [sdkconfig.defaults](../firmware/sdkconfig.defaults), [partition CSV](../firmware/partitions.csv), [asset CMake](../firmware/components/http_api/CMakeLists.txt), [packer](../tools/pack-web.py), [provenance](../tools/release_provenance.py).
- **SENSOR:** [types/validators](../firmware/components/monitor_types/monitor_types.cpp), [physical bus](../firmware/components/onewire_service/onewire_service.cpp), [sampling](../firmware/components/sampling_service/sampling_service.cpp), [registry](../firmware/components/sensor_registry/sensor_registry.cpp), [alerts](../firmware/components/alert_service/alert_service.cpp), [pins](../firmware/components/board_support/include/board_pins.h).
- **SETTINGS:** [sensor codec](../firmware/components/monitor_core/settings_codec.cpp), [sensor NVS](../firmware/components/settings_service/settings_service.cpp), [device NVS](../firmware/components/settings_service/device_settings_service.cpp), [options/TZ rules](../firmware/components/monitor_core/device_options.cpp).
- **HISTORY:** [aggregator](../firmware/components/monitor_core/monitor.cpp), [segment store](../firmware/components/monitor_core/history.cpp), [event journal](../firmware/components/monitor_core/event_store.cpp), [reducer/gaps](../firmware/components/monitor_core/history_query.cpp), [CSV](../firmware/components/monitor_core/history_csv.cpp), [worker](../firmware/components/history_service/history_service.cpp).
- **DISPLAY:** [model/cache](../firmware/components/monitor_core/display_model.cpp), [view](../firmware/components/display_service/display_view.cpp), [runtime](../firmware/components/display_service/display_runtime.cpp).
- **WIFI:** [worker](../firmware/components/wifi_service/wifi_service.cpp), [AP policy](../firmware/components/wifi_service/include/wifi_ap_window.hpp), [scan policy](../firmware/components/wifi_service/include/wifi_scan_window.hpp), [credential codec](../firmware/components/wifi_service/include/wifi_credentials.hpp), [captive DNS](../firmware/components/captive_service/captive_service.cpp).
- **CLOCK:** [SNTP/time](../firmware/components/clock_service/clock_service.cpp), [button/display timing](../firmware/components/monitor_core/monitor.cpp), [button/USB worker](../firmware/components/provisioning_service/provisioning_service.cpp).
- **API:** [dispatcher/snapshot](../firmware/components/http_api/http_api.cpp), [contracts](../contracts), [generated clients](../web/src/api), [core types](../firmware/components/monitor_types/include/monitor_types.hpp).
- **WS:** [broadcaster](../firmware/components/ws_live/ws_live.cpp), [frame sender](../firmware/components/ws_live/ws_frame.cpp), [browser subscriber](../web/src/api/live.ts).
- **SECURITY:** [auth](../firmware/components/auth_service/auth_service.cpp), [sessions](../firmware/components/monitor_core/sessions.cpp), [design decision](SECURITY_DESIGN.md), [AuthGate](../web/src/AuthGate.tsx), [auth client](../web/src/api/auth.ts).
- **OTA:** [upload/reset](../firmware/components/ota_service/ota_service.cpp), [health policy](../firmware/components/ota_service/include/ota_boot_policy.hpp), MAIN.
- **WEB:** [App](../web/src/App.tsx), [features](../web/src/features), [API clients](../web/src/api), [CSS](../web/src/styles.css), [package scripts](../web/package.json).
- **TEST:** [native targets](../firmware/tests/CMakeLists.txt), [core behavior](../firmware/tests/core_tests.cpp), [alert tests](../firmware/tests/alert_service_tests.cpp), [storage regressions](../firmware/tests/storage_tests.cpp), [history queries](../firmware/tests/history_query_tests.cpp), [headless rendering](../firmware/display_tests/display_tests.cpp), [verification script](../tools/verify.sh), frontend `*.test.ts(x)`, and Python `tools/tests/`.
- **DEVICE:** [normal smoke](validation/device-smoke-2026-10-30.txt), [API](validation/device-api-results.txt), [simulation on MCU](validation/device-simulation-results.txt), [OTA](validation/device-ota-results.txt), [health rollback](validation/device-ota-health-gate-results.txt), [BOOT](validation/device-boot-results.txt), [captive](validation/device-captive-results.txt). All are prior observations with their own stated limits.
- **DOC:** [SPEC](SPEC.md), [LCD design](LCD_UI_DESIGN_SPEC.md), [web design](WEB_UI_DESIGN_SPEC.md), [wiring](ESP32_S3_TEMPERATURE_MONITOR_WIRING.md), [development](DEVELOPMENT.md), [user guide](USER_GUIDE.md), [onboarding](ONBOARDING.md), [history format](HISTORY_FORMAT.md), [reliability](RELIABILITY.md), [handoff](HANDOFF.md), [release notes draft](RELEASE_NOTES_DRAFT.md).

## Complete numbered task audit

Tracker status below reproduces the existing checkbox, without changing it. **Implemented** means source-level implementation is supported by the cited code/tests; it does not certify unperformed physical acceptance. **Partial** and **Incorrect** on checked rows identify discrepancies. Evidence keys resolve to concrete source/test files in the index above.

| Task | Tracker | Verdict | Evidence and limits |
|---|---|---|---|
| T00-01 | checked | Implemented | DOC: required specifications and SVG/PNG assets exist; import history recorded. Remote rendering not newly exercised. |
| T00-02 | checked | Implemented | BUILD: DEVELOPMENT.md gives pinned SDK installation, activation, clean build and flash commands. |
| T00-03 | checked | Partial | DOC: evidence is extensive, but checked tasks below have implementation defects or incomplete acceptance. |
| T00-04 | checked | Implemented | TEST: native/simulator/display suites run independently of peripherals; hardware limits explicitly recorded. |
| T00-05 | checked | Implemented | BUILD: exact SDK revision, component manifest/lockfile, package-lock and CI pins present; physical compatibility still gated. |
| T00-06 | checked | Implemented | DOC, SENSOR, HISTORY: canonical wiring uses GPIO4/shared bus and 300-second aggregation. |
| T01-01 | checked | Implemented | BUILD: ESP32-S3, 16 MiB flash, octal PSRAM and versioned ESP-IDF project; prior exact-commit CI build passed. |
| T01-02 | checked | Implemented | MAIN: named ESP-IDF component boundaries exist; registry, sampling and alert components are distinct. |
| T01-03 | checked | Implemented | MAIN: startup errors, reset reason, PSRAM/heap, version and health logging; recorded bare-board smoke. |
| T01-04 | checked | Implemented | WEB: React/TypeScript/Vite scaffold; fresh lint, tests, typecheck and production build pass. |
| T01-05 | checked | Implemented | WEB: MUI theme, rounded surfaces and semantic colors; package lock pins dependencies. |
| T01-06 | checked | Implemented | BUILD, TEST: clang-format, Prettier, ESLint and CI verification workflow exist and checks pass. |
| T01-07 | checked | Implemented | TEST: fake_bus, native monitor_sim, Node HTTP/WS simulator and headless LVGL fixtures. |
| T01-08 | checked | Implemented | API: shared monitor_types, versioned JSON schemas and generated TypeScript clients. |
| T01-09 | checked | Implemented | BUILD, DEVICE: image/headroom reports and heap telemetry exist; physical panel refresh remains unverified. |
| T02-01 | open | Partial | DEVICE: recorded flash/PSRAM/native USB identity; physical markings, second port and orientation still need inspection. |
| T02-02 | open | Unverifiable | DOC: conditional backlight wiring documented; no voltage/current/header measurements available. |
| T02-03 | open | Unverifiable | DOC: ST7789 assumptions explicit; real addressing, screen size, color and regulator budget unmeasured. |
| T02-04 | checked | Implemented | SENSOR: board_pins.h centralizes DQ4, SCLK12, MOSI11, CS10, DC9, RST14, conditional BL15 and BOOT0. |
| T02-05 | checked | Implemented | SENSOR, BUILD: one RMT-backed bus, pinned onewire_bus/ds18b20 dependencies; physical timing unverified. |
| T02-06 | checked | Implemented | SENSOR: valid_rom checks family and CRC; discovery sorts/deduplicates ROMs; powered wiring remains a hardware prerequisite. |
| T02-07 | checked | Implemented | SENSOR: SKIP ROM Convert T, 750 ms yield, completion bit, MATCH ROM scratchpad and CRC decode. |
| T02-08 | checked | Implemented | MAIN, API: five-second delay-until acquisition, asynchronous history queues, mutex-protected published copies. |
| T02-09 | checked | Implemented | SENSOR: conversion-complete flag, CRC, finite/range checks; 85 C accepted only after completed conversion. Hardware sentinel behavior unverified. |
| T02-10 | checked | Incorrect | SENSOR: bounded rescans and stable identities exist; F1 prevents replacement at capacity and F2 hides excess devices. |
| T02-11 | checked | Partial | SENSOR: low-line/operation/all-transaction failure detection and bounded rescan exist; physical stuck-high/no-presence discrimination not established. |
| T02-12 | checked | Implemented | SENSOR: AlertService applies calibration before thresholds and aggregate/live presentation; canonical machine values use Celsius. |
| T02-13 | checked | Implemented | TEST: core and alert suites cover permutations, duplicates, CRC, conversion flag, faults and sixth ROM at registry layer; driver excess visibility gap F2 remains. |
| T02-14 | open | Unverifiable | DOC: no physical resistor/probe wiring is present to inspect or measure. |
| T02-15 | open | Unverifiable | DEVICE: numeric diagnostic collector exists and is tested; no real probes or physical cable measurements. |
| T03-01 | checked | Implemented | SENSOR: Settings carries ROM-associated label, short label, order, calibration, thresholds, persistence, hysteresis and disconnect preference. |
| T03-02 | checked | Partial | SETTINGS: bounded checked TMS1 codec, candidate registry and NVS commit; F4 leaves sensor edits misleadingly writable after load failure. |
| T03-03 | checked | Incorrect | SENSOR, SETTINGS: persistence and reordering preserve identity; new ROM cannot be managed after five saved identities (F1). |
| T03-04 | checked | Implemented | SENSOR: shared State enum and AlertService transitions feed one published registry; stale expiry helper is not scheduled independently by main. |
| T03-05 | checked | Implemented | SENSOR: optional thresholds, monotonic persistence, hysteresis, failure debounce and global bus event. |
| T03-06 | checked | Implemented | SENSOR, DISPLAY: registry retains expected missing ROMs; unused display rows differ from disconnected configured probes. |
| T03-07 | checked | Implemented | SENSOR, HISTORY: bounded transition events and durable journal sequence/deduplication; acknowledgement is optional and absent. |
| T03-08 | checked | Implemented | MAIN, API, DISPLAY: sample-cycle publishing/rendering independent of five-minute persistence. |
| T03-09 | checked | Implemented | TEST: alert_service_tests/core tests cover disable/re-enable, hysteresis, calibration, disconnect/reconnect and shared faults. |
| T03-10 | checked | Partial | SETTINGS, TEST: codec corruption, unsupported schema, duplicate identities and ordering covered; ESP-IDF NVS error-to-API path misses F4. |
| T04-01 | open | Partial | DISPLAY: ST7789/SPI and RGB/corner diagnostic implementation exists; physical pixels/offsets/colors not verified. |
| T04-02 | open | Unverifiable | DOC: BL safety gate exists; actual drive circuit and Wi-Fi TX supply measurement unavailable. |
| T04-03 | open | Partial | DISPLAY: PSRAM/DMA double buffers, LVGL task and locks configured; physical DMA/refresh/heap acceptance still open. |
| T04-04 | checked | Implemented | DISPLAY: two screen trees, five ordered labeled rows, values, status, Wi-Fi and clock; headless render passes. |
| T04-05 | checked | Implemented | DISPLAY: textual OFF/NO SENSOR/LOST/ERR/STALE/BUS FAULT distinct from color-only presentation. |
| T04-06 | checked | Implemented | DISPLAY: ROM-based trend cache and gap points, five read-only charts; physical appearance unverified. |
| T04-07 | checked | Implemented | DISPLAY: DisplayCycle defaults to Overview; extended mode alternates only Overview and Trends. |
| T04-08 | checked | Implemented | DISPLAY, SETTINGS: independent validated 5..300 second dwell values and monotonic timing; main applies updates at sample cadence. |
| T04-09 | checked | Implemented | DISPLAY: temporary provisioning screen receives per-device credentials and URL; not an ordinary selectable page. |
| T04-10 | checked | Implemented | DISPLAY: nonblocking network status/footer and temporary setup view; no acquisition wait for Wi-Fi. |
| T04-11 | checked | Implemented | CLOCK, DISPLAY: empty trusted time maps to --:--; configured POSIX TZ drives localtime after SNTP. |
| T04-12 | checked | Implemented | DISPLAY: persistent LVGL objects, changed-label checks and bounded RAM trend cache; no flash reads per frame. |
| T04-13 | checked | Implemented | TEST: headless LVGL renders and stored mock screenshots cover zero-to-five probes, warnings, long/negative values and no data. |
| T04-14 | open | Unverifiable | DEVICE: no panel for real legibility, view cycling or physical soak. |
| T05-01 | checked | Implemented | WIFI: saved station decode, worker events, bounded exponential reconnect; recorded association evidence. |
| T05-02 | checked | Implemented | WIFI: MAC-suffixed SSID plus random persisted WPA2 key; no universal secret. |
| T05-03 | checked | Partial | WIFI, WEB: local setup/auth/network forms and captive redirects exist; phone captive usability remains unverified under T05-18. |
| T05-04 | checked | Implemented | WIFI: candidate queue, AP+STA, DHCP SSID check and delayed handoff; failure restores prior station credentials. |
| T05-05 | checked | Implemented | WIFI: explicit NVS save after DHCP; distinct storage_error and AP grace period; previous device evidence retained. |
| T05-06 | checked | Implemented | WIFI: fixed mDNS hostname/service with nonfatal failure and documented IP fallback. |
| T05-07 | checked | Implemented | API, WEB: authenticated/CSRF-protected network candidate and provisioning endpoints with settings controls. |
| T05-08 | checked | Implemented | WIFI: rate-limited asynchronous scan, 20-result bound, station codec validation and credential-safe ordinary logs. |
| T05-09 | checked | Implemented | CLOCK: BootButton uses monotonic 30 ms debounce, 5-second hold, release arming and single trigger. |
| T05-10 | open | Partial | MAIN, DISPLAY: button worker publishes progress, but render polls it only every acquisition cycle (about 5 seconds); LCD/probe continuity unverified. |
| T05-11 | checked | Implemented | WIFI: BOOT only opens AP; candidate failure preserves NVS and prior station. Device evidence covers preservation. |
| T05-12 | checked | Implemented | TEST: core controls and AP-window regressions cover bounce, held-at-boot and independent recovery/handoff timers. |
| T05-13 | open | Partial | DEVICE: prior manual download/reset and hold evidence exists; photographed board/second USB routing still missing. |
| T05-14 | checked | Implemented | CLOCK, SETTINGS: SNTP callback, supported timezone/DST rules and configurable NTP hostname/IP; no exposed last-sync-age metadata. |
| T05-15 | checked | Implemented | CLOCK: trust begins only at SNTP callback and survives temporary Wi-Fi loss; cold boot has no trusted UTC. |
| T05-16 | checked | Implemented | HISTORY: boot ID/uptime with optional UTC; unknown-clock records remain explicitly boot-relative. No automatic UTC rebase is claimed. |
| T05-17 | checked | Implemented | HISTORY, TEST: monotonic display cycles and aggregator UTC-boundary/backstep policy tested. |
| T05-18 | open | Partial | DEVICE: stored station, failure preservation and captive endpoint evidence; real phone, secured wrong-password and router-offline recovery not qualified. |
| T06-01 | checked | Implemented | BUILD: partition CSV fits 16 MiB, two aligned 4 MiB OTA slots, NVS/otadata/PHY and 4 MiB history; provenance cross-checks binary table. |
| T06-02 | checked | Implemented | BUILD, DEVICE: image size/headroom measured and both slots bounded; pinned CI compiled current source. |
| T06-03 | checked | Implemented | HISTORY: mount never autoformats nonblank storage; empty-only initialization and healthy status; real full/corrupt media qualification open. |
| T06-04 | checked | Implemented | HISTORY: TMH2 records have CRC, bounded labels, ROM, avg/min/max/count, partial/state flags and optional UTC; legacy TMH1 decoder retained. |
| T06-05 | checked | Implemented | HISTORY: Aggregator accumulates 300-second windows; main submits only completed buckets. |
| T06-06 | checked | Implemented | HISTORY: UTC alignment, quality changes, monotonic boot-relative fallback and backward-jump avoidance; reboot partial window not falsely complete. |
| T06-07 | checked | Implemented | HISTORY, SENSOR: only enabled/fresh valid readings count; zero-count/partial/state metadata retained and tests cover gaps. |
| T06-08 | checked | Implemented | HISTORY: bounded segments, CRC/version validation, startup scan, interrupted-tail repair and corruption preservation; device power-loss gate remains. |
| T06-09 | checked | Implemented | HISTORY: 3 MiB logical normal budget and 32 KiB segments reserve physical headroom; oldest segments evicted after durable new record. |
| T06-10 | checked | Implemented | HISTORY: bounded separate event journal and transition deduplication; actual load/wear qualification remains open. |
| T06-11 | checked | Implemented | HISTORY, API: bounded queries, visitor-based CSV streaming, explicit time quality/name source and formula escaping; full device retention timing unverified. |
| T06-12 | checked | Incorrect | HISTORY: bounded weighted downsampling preserves extrema, but loses exact intra-group gap intervals (F3). |
| T06-13 | checked | Implemented | DISPLAY: bounded deque of recent aggregates; LVGL reads RAM without repeated LittleFS reads. |
| T06-14 | open | Partial | HISTORY, TEST, DEVICE: exhaustive host torn-write and corruption checks plus simulated-device queries pass; physical full media, wear, power loss and large-store concurrency remain. |
| T06-15 | open | Partial | HISTORY, DEVICE: record-size forecasts and fake-provider LittleFS evidence exist; actual physical-probe retention/oldest/latest measurements absent. |
| T06-16 | open | Unverifiable | DEVICE: no controlled physical power-interruption evidence while real probes log. |
| T07-01 | checked | Partial | API: URI/body/socket bounds and ESP-IDF HTTP/WS enabled; request-wide body time bound is missing (F6). |
| T07-02 | checked | Implemented | API: v1 contracts and shared Celsius/ROM representations; schemas and generated TS validated in suites. |
| T07-03 | checked | Partial | API, SETTINGS: snapshots, sensor GET/PATCH validation, etags and queued edits; persistence-failure honesty missing (F4). |
| T07-04 | checked | Implemented | API, SETTINGS: versioned device preferences, etags, validation and read-only state for bad device-options NVS. |
| T07-05 | checked | Incorrect | API, HISTORY: bounds and streamed CSV exist; downsampled explicit gaps lose internal holes (F3). |
| T07-06 | checked | Implemented | HISTORY, API: active faults and journal cursor/oldest/health endpoints; page integrity tests and recorded real API evidence. |
| T07-07 | checked | Implemented | WIFI, API: asynchronous candidate admission, overlap rejection and delayed station disconnect. |
| T07-08 | checked | Implemented | WS: full snapshots, four sessions, coalesced queue work and bounded nonblocking sender; device backpressure evidence. |
| T07-09 | checked | Implemented | WS, WEB: REST resync, boot/sequence monotonicity, bounded frames and forbidden command rejection. |
| T07-10 | checked | Implemented | TEST, DEVICE: malformed/forbidden/bounds, four-client admission and 120-second stalled WS acceptance recorded; does not cover slow HTTP body F6. |
| T08-01 | checked | Implemented | SECURITY: explicit owner-selected HTTP model, bootstrap enrollment and physical credential recovery. |
| T08-02 | checked | Implemented | WIFI, SECURITY: cryptographically random persistent AP password and guarded configuration storage. |
| T08-03 | checked | Implemented | SECURITY: 100000-iteration PBKDF2, salt, known-answer test, constant-time comparison, bounded cooldown and rotation. |
| T08-04 | checked | Implemented | SECURITY, API: bounded one-hour sessions, CSRF/Host/Origin gates and narrowly scoped captive probe exception. |
| T08-05 | checked | Implemented | API: sensitive reads, mutation, live handshake and OTA behind authorization; auth-state bootstrap disclosure narrowly scoped. |
| T08-06 | checked | Implemented | SECURITY: accepted local HTTP confidentiality limitation is explicit; no false HTTPS claim. |
| T08-07 | checked | Implemented | SECURITY, WEB: no browser credential persistence or ordinary credential logging; explicit physical USB secret/bridge responses are sensitive by design. |
| T08-08 | checked | Partial | TEST, DEVICE: session/CSRF/origin/OTA/lockout tests exist; slow unauthenticated HTTP body starvation remains untested/unmitigated (F6). |
| T09-01 | checked | Implemented | WEB: responsive rail/bottom navigation, theme controls, keyboard-native controls, local system fonts and reduced-motion CSS. |
| T09-02 | checked | Implemented | WEB: Ajv validation, reconnect backoff, heartbeat timeout, stale values and REST boot/sequence resync. |
| T09-03 | checked | Partial | WEB: live cards/states/age/status implemented; recent sparkline optional; dashboard history text always says no history even when history exists. |
| T09-04 | checked | Implemented | WEB: per-card textual high/low/disconnected indicators and nonblocking shared-bus banner; unused/disabled distinct. |
| T09-05 | checked | Incorrect | WEB, HISTORY: selectors/series/min-max charts exist; configured timezone ignored (F5) and downsampled internal gaps lost (F3). |
| T09-06 | checked | Implemented | WEB: lazy route modules, bounded SVG chart and button-based bounded zoom/pan. |
| T09-07 | checked | Implemented | WEB, HISTORY: authenticated CSV link and five-minute aggregate labeling; canonical CSV units are Celsius. |
| T09-08 | checked | Implemented | WEB: per-ROM editor with etags, numeric validation, calibration, order, labels and alert controls; persistence caveat F4. |
| T09-09 | checked | Implemented | WEB: physical warming identification guidance; labels keyed by immutable ROM. |
| T09-10 | checked | Partial | WEB: active/global faults, cursor paging and retention warning; timestamps ignore configured device timezone (F5). |
| T09-11 | checked | Implemented | WEB: Wi-Fi, timezone/NTP server, units, fixed logging policy, storage and firmware diagnostics; last SNTP sync time not exposed. |
| T09-12 | checked | Implemented | WEB, DISPLAY: Basic/Extended, 20/10 defaults, bounded independent dwell/lookback, read-only explanation; hide-disabled control optional. |
| T09-13 | checked | Partial | WEB, WIFI: self-hosted auth/setup/network flow and retry handling; actual phone captive/onboarding gate unverified. |
| T09-14 | checked | Implemented | WEB: enrollment/login/session-expiry/password rotation flows with no browser localStorage; initial auth-state transport failure needs reload. |
| T09-15 | checked | Implemented | WEB, API: firmware/storage status, guarded update/reboot and two explicit confirmed reset choices. |
| T09-16 | checked | Partial | WEB, DOC: responsive CSS and prior browser measurement evidence exist; no fresh full-route 360/1280 accessibility/layout measurement in this review. |
| T09-17 | checked | Partial | TEST: 56 tests across 16 files cover many listed scenarios; not an exhaustive every-page-by-every-state matrix; targeted bugs escape the suite. |
| T09-18 | checked | Implemented | WEB, BUILD: hashed split local assets; gzip packer/catalog and server encoding/cache headers; no runtime CDN requirement. |
| T09-19 | checked | Implemented | BUILD, API: linked embedded asset table, known SPA fallbacks and API 404; no Node runtime on MCU. |
| T09-20 | checked | Partial | DOC, DEVICE: bundle/browser measurements documented and current build sizes match; full physical sensor/panel socket/heap acceptance open under T10-05. |
| T10-01 | checked | Implemented | OTA: bounded authenticated inactive-slot write, ESP-IDF validation, boot selection and delayed reboot; prior A/B device upload evidence. |
| T10-02 | open | Partial | OTA, MAIN: two cycles and required-service startup predicate; injected pending/rollback evidence exists; true component failure and LCD visibility unverified. |
| T10-03 | checked | Implemented | BUILD, OTA: assets linked in same app image; CMake repacks changed inputs and prior device OTA served corresponding embedded assets. |
| T10-04 | checked | Implemented | OTA, HISTORY, DOC: interrupted upload abort, pending rollback and documented recovery preserve history; actual power interruption still open. |
| T10-05 | open | Partial | DEVICE: bare-board and five-fake-provider 120-second load telemetry; real sensors/panel, full history and long-run memory behavior still unverified. |
| T10-06 | checked | Implemented | DOC, MAIN: HTTP tradeoff, watchdog/reset/heap diagnostics and safe peripheral defaults documented. |
| T10-07 | checked | Implemented | TEST: burst events, replacement below capacity, reconnect, ENOSPC, clock jumps, corrupt codecs and accelerated day sampling; full-capacity replacement F1 not covered. |
| T10-08 | open | Unverifiable | DEVICE: no 24-72 hour five-real-probe/LCD/Wi-Fi stability evidence. |
| T10-09 | open | Partial | OTA, DEVICE: USB and A/B/rollback with credentials/journal preservation recorded; populated real temperature-history equality absent. |
| T10-10 | open | Partial | DEVICE: prior BOOT setup with healthy storage/uptime; actual probe continuity and router-offline recovery absent. |
| T11-01 | open | Partial | DOC: core pinout/bucket policy consistent; actual measured hardware reconciliation absent and contributor guide is stale (F7). |
| T11-02 | checked | Implemented | DOC: shared pull-up, polarity/common-ground/power/BL warnings and optional breakout guidance; electrical measurements still separate. |
| T11-03 | checked | Implemented | DOC, BUILD: SDK activation/build/flash/partitions and known USB recovery roles documented; unknown second port explicitly flagged. |
| T11-04 | checked | Implemented | DOC: first-run/USB/bootstrap, hold recovery, accepted HTTP limitations and remaining phone checks documented. |
| T11-05 | checked | Implemented | DOC: sensor calibration/labels, alerts, display options, timezone and web history/reset behavior documented. |
| T11-06 | checked | Implemented | DOC: binary schema, retention forecast, boot-relative time, CSV, backup/repair/reset/OTA recovery documented. |
| T11-07 | checked | Implemented | TEST, DEVICE: reproducible scripts and sanitized device evidence distinguish simulation and physical qualification. |
| T11-08 | open | Partial | BUILD, DOC: CI-only artifact manifest/checksums and draft release notes; release is correctly blocked on physical acceptance/signoff. |
| T11-09 | checked | Partial | DOC: handoff and optional list present; literal no-leftover-blockers claim is unmet and hardware-only remaining-work summary misses the software findings here. |

Audit verdict counts: 103 implemented, 5 incorrect, 29 partial, 8 unverifiable. These are review verdicts, not edited tracker totals.

## Nested follow-up audit

The 41 dated child bullets contain concrete software follow-ups, although many are historical progress notes rather than new unchecked requirements. Each is audited below. “Implemented” certifies the narrow software follow-up only; it does not close its parent physical/release gate. Inode/symlink checks marked host-only do not establish equivalent ESP32 LittleFS identity guarantees.

| Child requirement (original label) | Verdict | Source and verification |
|---|---|---|
| 2026-10-28 software-only handoff regression | Implemented | WIFI: WifiApWindow candidate/BOOT/reconnect deadlines and worker calls; wifi_ap_window_tests.cpp passed. Phone/physical recovery acceptance remains separate. |
| 2026-10-28 software-only BOOT/handoff timer isolation | Implemented | WIFI: WifiApWindow candidate/BOOT/reconnect deadlines and worker calls; wifi_ap_window_tests.cpp passed. Phone/physical recovery acceptance remains separate. |
| 2026-10-28 software-only scan-completion isolation | Implemented | WIFI: WifiScanWindow completion/deadline policy and worker integration; wifi_scan_window_tests.cpp passed. Actual all-order radio races not proven. |
| 2026-10-28 software-only late scan timeout release | Implemented | WIFI: WifiScanWindow completion/deadline policy and worker integration; wifi_scan_window_tests.cpp passed. Actual all-order radio races not proven. |
| 2026-10-28 software-only failed-scan status regression | Implemented | WIFI: WifiScanWindow completion/deadline policy and worker integration; wifi_scan_window_tests.cpp passed. Actual all-order radio races not proven. |
| 2026-10-29 journal compaction pre-rename integrity (software-only) | Implemented | HISTORY: EventStore fingerprint/temporary validation around fsync and rename; event_append_integrity_tests.cpp and storage_tests.cpp passed. |
| 2026-10-29 completed-segment eviction integrity (software-only) | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-29 early-stop history integrity (host-only) | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-29 post-fsync event-journal integrity (software-only) | Implemented | HISTORY: EventStore fingerprint/temporary validation around fsync and rename; event_append_integrity_tests.cpp and storage_tests.cpp passed. |
| 2026-10-29 active history append integrity (software-only) | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-29 cross-segment export integrity (host-only) | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-29 host-only export integrity | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-28 software-only write-failure guard | Implemented | HISTORY: both HistoryStore and EventStore append_ready_ latches and recovery; storage_tests.cpp and torn-write suites passed; no physical power-loss claim. |
| 2026-10-28 software-only event sequence integrity | Implemented | HISTORY: EventStore recovery/append/page/compact guards; storage_tests.cpp, event_power_cut_tests.cpp and event_append_integrity_tests.cpp passed. Physical interruption remains unverified. |
| 2026-10-28 software-only recovery latch | Implemented | HISTORY: both HistoryStore and EventStore append_ready_ latches and recovery; storage_tests.cpp and torn-write suites passed; no physical power-loss claim. |
| 2026-10-28 software-only oversized-journal guard | Implemented | HISTORY: EventStore recovery/append/page/compact guards; storage_tests.cpp, event_power_cut_tests.cpp and event_append_integrity_tests.cpp passed. Physical interruption remains unverified. |
| 2026-10-28 software-only partial event header guard | Implemented | HISTORY: EventStore recovery/append/page/compact guards; storage_tests.cpp, event_power_cut_tests.cpp and event_append_integrity_tests.cpp passed. Physical interruption remains unverified. |
| 2026-10-28 software-only partial history header guard | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-28 software-only pre-recovery write guard | Implemented | HISTORY: both HistoryStore and EventStore append_ready_ latches and recovery; storage_tests.cpp and torn-write suites passed; no physical power-loss claim. |
| 2026-10-28 host-only journal path safety | Implemented | HISTORY: EventStore recovery/append/page/compact guards; storage_tests.cpp, event_power_cut_tests.cpp and event_append_integrity_tests.cpp passed. Physical interruption remains unverified. |
| 2026-10-29 software-only history path integrity | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-29 software-only event-journal path integrity | Implemented | HISTORY: EventStore recovery/append/page/compact guards; storage_tests.cpp, event_power_cut_tests.cpp and event_append_integrity_tests.cpp passed. Physical interruption remains unverified. |
| 2026-10-29 software-only retention safety | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-29 software-only history rollover path guard | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-29 software-only history directory guard | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-29 host-only history export identity | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-29 software-only history append directory identity | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-29 host-only active-segment identity | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-29 host-only event-journal identity | Implemented | HISTORY: EventStore recovery/append/page/compact guards; storage_tests.cpp, event_power_cut_tests.cpp and event_append_integrity_tests.cpp passed. Physical interruption remains unverified. |
| 2026-10-29 software-only last-record guard | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-29 software-only API page integrity | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-29 software-only bounded recovery | Implemented | HISTORY: EventStore recovery/append/page/compact guards; storage_tests.cpp, event_power_cut_tests.cpp and event_append_integrity_tests.cpp passed. Physical interruption remains unverified. |
| 2026-10-29 host-only 64-bit event-cursor exhaustion | Implemented | HISTORY: EventStore recovery/append/page/compact guards; storage_tests.cpp, event_power_cut_tests.cpp and event_append_integrity_tests.cpp passed. Physical interruption remains unverified. |
| 2026-10-29 software-only history export integrity | Implemented | HISTORY: HistoryStore recover/append/visit, segment seals, retention and latches; storage_tests.cpp and history_power_cut_tests.cpp passed. Physical interruption and full-store latency remain unverified. |
| 2026-10-29 software-only earlier-record integrity | Implemented | HISTORY: EventStore recovery/append/page/compact guards; storage_tests.cpp, event_power_cut_tests.cpp and event_append_integrity_tests.cpp passed. Physical interruption remains unverified. |
| 2026-10-29 CI-only packaging subtask | Implemented | BUILD: release_provenance.py and tools/tests/test_release_provenance.py; CI upload/download/checksum steps. Fresh 14-test Python suite passed; release remains blocked. |
| 2026-10-29 complete flash-layout provenance follow-up (software-only) | Implemented | BUILD: release_provenance.py and tools/tests/test_release_provenance.py; CI upload/download/checksum steps. Fresh 14-test Python suite passed; release remains blocked. |
| 2026-10-29 flashed partition-table binary cross-check (software-only) | Implemented | BUILD: release_provenance.py and tools/tests/test_release_provenance.py; CI upload/download/checksum steps. Fresh 14-test Python suite passed; release remains blocked. |
| 2026-10-29 exact flash-image allowlist and CSV flags (software-only) | Implemented | BUILD: release_provenance.py and tools/tests/test_release_provenance.py; CI upload/download/checksum steps. Fresh 14-test Python suite passed; release remains blocked. |
| 2026-10-29 CI artifact completeness (software-only) | Implemented | BUILD: release_provenance.py and tools/tests/test_release_provenance.py; CI upload/download/checksum steps. Fresh 14-test Python suite passed; release remains blocked. |
| 2026-10-29 preparatory release notes (software-only) | Implemented | DOC: RELEASE_NOTES_DRAFT.md explicitly withholds release approval; final physical qualification/signoff remains missing. |

## Phase gates and definition of done

| Gate | Verdict | Evidence and remaining requirement |
|---|---|---|
| Phase 1: build/web/tests and diagnosable real boot | Implemented | Fresh host/display/web verification and exact-SHA firmware CI; historical real-board READY/smoke evidence. No new board boot during review. |
| Phase 2: stable real/simulated identities, honest readings and one bus fault | Partial | Host identity/fault tests pass; replacement/excess integration failures F1/F2 and physical probes remain. |
| Phase 3: common fault state, persistence, malformed-data rejection | Partial | Shared registry and guarded codecs; corruption/persistence honesty defect F4. |
| Phase 4: two read-only views and graceful setup state | Partial | Headless LVGL and view-cycle implementation pass; no real panel/legibility evidence. |
| Phase 5: offline provisioning, flashing-safe BOOT and independent acquisition/time | Partial | Policy/device subset recorded; phone, router-offline and real-probe continuation remain unverified. |
| Phase 6: five-minute temperature records, separate events, restart/interrupted-write persistence | Partial | Code uses five-minute buckets and a separate journal; host torn-write and recorded restart checks pass; physical interrupted-write recovery remains unverified. |
| Phase 7: browser/simulator/device API and bounded transport | Partial | Host and historical board socket tests; slow HTTP body can monopolize networking (F6). |
| Phase 8: no release with unauthenticated configuration or unreviewed credential transport | Implemented | Configuration is authenticated, the owner explicitly accepted HTTP exposure, and CI artifacts withhold release approval. This does not resolve availability issue F6 or certify release readiness. |
| Phase 9: phones/desktops, configured settings and network interruptions | Partial | Component tests and responsive implementation; configured timezone F5, incomplete all-page/state matrix and real phone acceptance. |
| Phase 10: stability, recoverable OTA, local threat model and cloud independence | Partial | Prior OTA/backpressure evidence; actual combined hardware soak and power loss absent; software findings remain. |
| Phase 11 release qualification/delivery (derived from tasks; no separate phase-gate line) | Partial | CI-only provenance is implemented; qualified release blocked on unresolved findings, hardware gates and signoff. |
| DoD: every implementation subtask complete with evidence or named external blocker | Incorrect | New software findings cannot be classified as hardware-only blockers; checked tasks affected by F1–F6 need fixes/review. |
| DoD: five probes, one shared data pin and one pull-up work reliably | Unverifiable | Needs five powered DS18B20 probes, one verified pull-up and physical readings/latency/reconnect evidence. |
| DoD: LCD Basic/Extended follows design without touch/settings | Partial | Source/headless views implement this; physical electrical and visual acceptance missing. |
| DoD: HTTP/React live/history/alerts/settings/security | Partial | Substantial implementation exists; F3–F6 prevent full requirement-level acceptance. |
| DoD: AP onboarding and BOOT recovery hardware proven, flashing intact | Partial | Prior USB/BOOT/captive endpoint evidence; phone/router-offline recovery and second-port/board evidence missing. |
| DoD: flash history/OTA/recovery demonstrate physical persistence/recoverability | Partial | Prior OTA/admin/Wi-Fi/journal checks; real populated temperature preservation and power interruption absent. |
| DoD: useful offline with no false temperature/UTC | Partial | Code rejects invalid values and uses explicit unknown time; real probes/router-loss test absent and F2 can cause false loss. |

## Optional backlog

These six items are explicitly outside v1; their absence is not a defect in this review.

| Optional item | Verdict | Inspection result |
|---|---|---|
| MQTT / Home Assistant | Deferred/optional | No required MQTT/HA integration in current services. |
| Remote alarms or buzzer | Deferred/optional | No required external notification/buzzer implementation. |
| Hardware RTC | Deferred/optional | No RTC service; trusted UTC intentionally absent after unsynchronized cold boot. |
| Alert schedules, weekly reports, multi-device dashboard | Deferred/optional | No corresponding required UI/service implementation. |
| Prometheus/OpenMetrics or local MQTT | Deferred/optional | Current routes implement the local REST/WS contract instead. |
| Dedicated enclosure/PCB/terminals with ESD protection | Deferred/optional | Mechanical/electrical extension outside the reviewed firmware implementation. |

## Resume points

F1–F7 provide concrete software/documentation work that can proceed without attaching peripherals. This review does not authorize or perform those fixes. Hardware acceptance still needs board markings/port identification, measured LCD/backlight electrical behavior, attached panel, one verified pull-up and real probes. Power-loss qualification additionally needs a controllable physical interruption; phone/router-offline and 24–72-hour soak require their specified equipment/conditions. T11-08 must remain blocked until required fixes, physical acceptance and owner signoff pass.

Historical CI run IDs and successful device reports are useful evidence, but were not exhaustively re-fetched or independently repeated. This audit does not certify field reliability, flash endurance, six-probe electrical behavior, browser/device timezone runtime rendering, or a qualified release.
