# ESP32-S3 Temperature Monitor — System Specification

**Status:** Approved architecture baseline; implementation in progress (see `PROGRESS.md`)  
**Version:** 1.0-draft  
**Date:** 2026-10-25  
**Target:** ESP32-S3-N16R8 (16 MB flash, 8 MB PSRAM), up to five DS18B20 probes, 240 × 320 ST7789 SPI LCD  
**Implementation:** **ESP-IDF v6.1** (C/C++), LVGL 9, React 19 + TypeScript + Vite, Material 3 design

> This document is the source of truth for **system-level behavior**. The accompanying wiring, LCD and web UI documents in this `docs/` directory define their respective interfaces. If the drawings differ from this specification, implement the **documented requirements**, not illustrative sample values. Hardware-specific uncertainties are explicitly marked below and must be resolved before electrical connection.

## 1. Vision and scope

Create a **local-first, self-contained, five-probe temperature monitor and historian** that works without an internet service or external computer. The ESP32 reads DS18B20 probes, shows a glanceable status on a small LCD, serves a mobile-friendly web app over Wi-Fi, logs compact five-minute temperature summaries to its own flash, and detects thermal alarms and missing sensors.

**Firmware SDK baseline:** **ESP-IDF v6.1** for ESP32-S3. All firmware and CI builds must use the official v6.1 release baseline, not `master`, an unpinned `latest`, or a different major/minor line. Record the resolved exact v6.1 source revision, installed toolchain, and component lockfile for reproducible builds. Patch upgrades within the 6.1 series require CI and on-device regression validation; moving to 6.2+ is a deliberate specification change.

**Core principles:**

- **One shared sensor bus:** up to five DS18B20 sensors on one externally pulled-up 1-Wire data line, identified by immutable ROM addresses.
- **Two independent presentation surfaces:** a **read-only** LVGL LCD and a full interactive, self-hosted **web application**. Both use the same authoritative sensor/alert/configuration state.
- **No cloud:** no required external account, remote server, web CDN, or subscription. NTP is optional for accurate clock time and a local NTP server may be used.
- **Graceful degradation:** continue reading probes, evaluating alarms, and recording available history if Wi-Fi fails; report lost sensors, a stuck bus, unsynchronized time or damaged/full storage honestly.
- **Flash-conscious logging:** live measurements approximately every 5 seconds; persist **only five-minute aggregate temperature windows** (average/minimum/maximum/count), plus separately timestamped notable events.
- **Web-only configuration:** no touch gestures, LCD menus or input peripherals beyond the existing BOOT button for Wi-Fi recovery.

### 1.1 In scope for v1

1–5 powered DS18B20 probes; automatic 1-Wire discovery; user labels; enable/disable; threshold and disconnection alerts; LVGL Overview/Trends views; captive-portal Wi-Fi setup; GPIO0 BOOT-button recovery; embedded React Material 3 web app; live updates; historical graphing and CSV export; persistent configuration and rolling logs; clock synchronization; local administrative authentication; OTA firmware update capability; testing and packaging.

### 1.2 Out of scope for v1

Cloud dashboards or remote access, MQTT/Home Assistant integrations, relay control, buzzers, SMS/push notification infrastructure, native mobile apps, touchscreen interaction, microSD, battery operation, and adding an external RTC. Keep extension points modest without implementing these features.

## 2. Physical hardware

### 2.1 Selected components

| Part | Qty | Use / assumptions |
|---|---:|---|
| ESP32-S3-N16R8 development board | 1 | 16 MB flash, 8 MB PSRAM; photographed 44-pin, dual-USB-C board |
| Waterproof DS18B20, three-wire probe | 1–5 | Externally powered at **3.3 V** (no parasite mode); approximately 1 m cable each |
| 4.7 kΩ resistor | **1** | Pulls the single 1-Wire DATA line up to 3.3 V |
| ST7789-class IPS LCD, 240 × 320 | 1 | SPI portrait, **read-only**; photographed PCB says **2.8-inch**, though screen was conversationally described as ~2-inch |
| Common wiring/junctions | As needed | Three-conductor shared bus and LCD SPI leads |
| USB-C power | 1 | Adequate 3.3 V regulator and 5 V source; measure power budget during bring-up |

The five small terminal breakout boards supplied with the probes are **not required**. Avoid adding five parallel pull-ups accidentally. Verify probe lead colors against the actual hardware, not only color conventions.

### 2.2 Wiring and GPIO allocation

The definitive wire-by-wire assembly reference is [`docs/ESP32_S3_TEMPERATURE_MONITOR_WIRING.md`](ESP32_S3_TEMPERATURE_MONITOR_WIRING.md), accompanied by [`docs/esp32_s3_temperature_wiring.svg`](esp32_s3_temperature_wiring.svg).

| Signal | ESP32-S3 pin | Destination |
|---|---|---|
| 1-Wire DQ / DATA | **GPIO4** | All five DS18B20 DATA leads **in parallel** |
| Sensor power | **3V3** | All five probe VDD leads |
| Sensor ground | **GND** | All five probe GND leads |
| 1-Wire pull-up | **one 4.7 kΩ** | Between 3V3 and GPIO4/DQ, **not in series** |
| LCD SPI SCLK | **GPIO12** | LCD SCL/SCK |
| LCD SPI MOSI | **GPIO11** | LCD SDA/MOSI |
| LCD chip select | **GPIO10** | LCD CS |
| LCD data/command | **GPIO9** | LCD DC |
| LCD reset | **GPIO14** | LCD RES/RST |
| LCD backlight | **GPIO15, conditional** | Only when verified as a 3.3 V logic-compatible BL/BLK input; otherwise use an appropriate external driver or board-defined supply |
| LCD power and ground | **3V3**, **GND** | LCD VCC/GND **only after checking module ratings** |
| Built-in BOOT input | **GPIO0** | Board's existing BOOT button; no new button |

**Mandatory hardware sign-off before full assembly:** verify actual LCD header names/order, LCD supply voltage and backlight voltage/current/polarity, exact panel/controller initialization requirements and the board's 3.3 V regulator capacity. A GPIO must **not** drive an unknown raw LED backlight load. GPIO0 is a boot strap: firmware reads it only **after normal boot**. Holding BOOT across reset remains the ROM's firmware-download procedure. Reserve native USB GPIO19/GPIO20 and internal memory pins.

### 2.3 1-Wire electrical design

- Wire sensors electrically **in parallel** on shared `3V3`, `DQ`, `GND` conductors. Physical trunk/daisy-chain layout is preferred to long multi-leg star branches where practical.
- Use one **4.7 kΩ DATA-to-3.3 V pull-up**, preferably near ESP32. Do not connect extra pull-ups from separate adapter boards unless effective resistance is checked.
- Use standard three-wire-powered DS18B20 conversion; issue bus-wide `Convert T` then read each ROM-addressed sensor's scratchpad with CRC verification. At 12-bit resolution, allow the datasheet's maximum conversion time (up to 750 ms); default live cycle is 5 s.
- Use Espressif-supported 1-Wire RMT backend if it passes integration tests: only **one** RMT-backed bus is needed. The backend is an implementation choice, not a functional dependency; compatible verified alternatives are acceptable.
- Discover up to five unique DS18B20 ROM IDs. Define and test deterministic behavior if additional devices are detected (report excess, never silently overwrite bindings).
- Detect stuck-low/stuck-high bus conditions and distinguish a **bus-wide fault** from one sensor's disappearance.
- Live wiring changes should be handled safely where feasible, but **hot-plug electrical stress protection is not guaranteed** by firmware. Power off when rewiring during development.

## 3. Software architecture

```text
DS18B20 x 1..5 -- shared 1-Wire/RMT on GPIO4
                     |
          sensor discovery + sample/CRC
                     |
         shared state / events (single truth)
             |         |          |
        alert FSM   5-min      display model
             |      buckets         |
             |         |          LVGL 9 -> ST7789
             |       history
             |      LittleFS
             +---------+-------------------+
                                       REST / WebSockets
                                            |
                        Wi-Fi STA / SoftAP + HTTP server
                                            |
                  React 19 + TypeScript + Vite + Material 3
                                (executes in browser)

NVS: network credentials, sensor ROM mappings, thresholds, LCD settings,
     timezone/admin configuration; separate from rotating historical logs.
```

Preferred firmware modules: `board_support`, `onewire_service`, `sensor_registry`, `sampling_service`, `alert_service`, `history_service`, `clock_service`, `settings_service`, `wifi_service`, `provisioning_service`, `http_api`, `ws_live`, `display_service`, `ota_service`, and `health_service`.

- Event/snapshot boundaries must be explicit and thread-safe. LVGL calls occur on the correct LVGL execution context; HTTP callbacks do not manipulate LVGL directly.
- Use finite queues and backpressure; an unresponsive browser, flash write, Wi-Fi scan or screen drawing must not delay probe sampling indefinitely.
- Keep sensor/alert logic testable on a host without the ESP32. Provide mocks for clock, storage, 1-Wire, networking and LVGL display data models.
- Use **ESP-IDF v6.1** as the mandatory firmware SDK baseline; pin the exact v6.1 release revision and toolchain in setup/CI rather than using `master` or an unpinned `latest`. Lock compatible component versions in the ESP-IDF component manifest/lockfile and verify them with both clean host builds and hardware tests. Build reproducibly using `idf.py` and the normal Vite build. No Docker requirement.

## 4. Sensor identity, state and sampling

### 4.1 Identity, discovery, labels

Each sensor is keyed by its DS18B20 **64-bit ROM ID**, **not its order returned during discovery**. The registry stores name, LCD-short-label (optional), enabled flag, display order, thresholds, last-seen time, and per-sensor calibration offset. On first discovery, assign neutral default names (Sensor 1, Sensor 2, etc.); allow renaming and identifying a physical probe by watching a deliberate temperature change in the web UI.

Never assume there must be five installed sensors. Zero installed and one through four installed are supported states. A newly discovered unassigned sensor must not inherit an unrelated removed sensor's label, threshold or history.

### 4.2 Acquisition cadence and data validation

- **Default full temperature acquisition cycle:** 5 seconds, tunable only within a safe validated range if later enabled; web/LCD publish on fresh valid results.
- Startup and reconnect must wait for a valid conversion, **not** mistake DS18B20 power-up 85°C for a measured temperature.
- Validate ROM/device family, scratchpad CRC, conversion completion and plausible datasheet temperature bounds; flag communication errors. Do not show sentinel temperatures such as -127°C as real data.
- Re-scan periodically, on observed failures, and after topology changes, with limits to avoid starving conversion. Persist identity mappings across restart.
- Store a monotonic acquisition timestamp alongside optional trusted UTC for freshness detection.

### 4.3 Authoritative sensor states

| State | Meaning | Consequence |
|---|---|---|
| `normal` | Enabled, valid, fresh and in-range | Display temperature; no active temperature alert |
| `high` / `low` | Valid fresh reading and alarm confirmed | Alert on LCD and web |
| `unassigned` | No expected ROM ID assigned to a slot | No false missing-sensor alert |
| `disabled` | Recognized sensor intentionally off | No threshold/disconnection alerts; configuration/history preserved |
| `disconnected` | Expected sensor repeatedly absent | Fault indication and event, subject to settings |
| `stale` / `invalid` | Too old or CRC/conversion errors | Never silently display invalid reading as current |
| `bus_fault` | Shared DQ line or bus communication unavailable | Show **one bus-wide fault**; don't spam five separate alarms |

Missing configured ROM IDs should remain visible as disconnected when enabled. A probe that has never been expected is simply absent/unassigned. Disabled mappings must not produce false disconnection alarms.

## 5. Temperature alarms and system health

- Per-ROM-ID configuration: optional lower/upper threshold, enabled/disabled alert, hysteresis, persistence duration, disconnect alerts and calibration offset.
- Evaluate approximately every new 5-second sample and on state changes, **not** at history-write boundaries. Default provisional debouncing: three consecutive probe failures for disconnect; 15 s persistence for threshold crossing; 0.5°C hysteresis to clear. Defaults are user-adjustable where supported and subject to testing.
- Alert transitions include `raised`, `acknowledged` (if supported), `cleared`, and `recovered`. Persistent on both UIs while active. The LCD does not require acknowledgement.
- Record notable events (threshold transitions, loss/recovery, bus faults, logging failures, time synchronization, Wi-Fi changes and reboot) **separately from five-minute temperature aggregates**.
- Alerts must be consistent across LVGL and REST/WebSocket views; use textual/icon cues as well as color. Alerts should not cover the overview with a blocking full-screen screen.
- An inactive/disconnected sensor never returns a manufactured 0°C reading. A global bus fault is visibly different from an individual sensor being unplugged.

## 6. LCD user experience — read-only

Full specification: [`docs/LCD_UI_DESIGN_SPEC.md`](LCD_UI_DESIGN_SPEC.md). Mockups: [`docs/LCD_UI_MOCKUPS.svg`](LCD_UI_MOCKUPS.svg) and `.png`.

- **Panel canvas:** 240 × 320 portrait; design for readability on a roughly 2-inch-class viewing experience. **Physical board printing reads “2.8 IPS 240×320”; verify diagonal**.
- **Basic mode (default):** persistently show one **Overview** screen with up to five rows: short name, current value and warning/disabled/disconnected indicator. Also show Wi-Fi connected/offline indicator and local clock; minimal logging-health cue.
- **Extended mode:** automatically cycle **Overview → Trends → Overview**. Trends has five compact labeled sparklines (default lookback 60 minutes). No axes/toolbars/input targets; the browser has the detailed interactive charts.
- **Default dwell:** Overview **20 s**, Trends **10 s**, independently configurable from web (suggested range 5–300 s). Basic mode does not cycle. First view is Overview. Use monotonic time for cycling.
- **Provisioning system state:** temporarily display AP SSID, device-specific password/setup code as designed, and `http://192.168.4.1`. Not a third normal mode. Provide short, nonblocking status feedback for connecting/recovery.
- **Clock:** show localized `HH:MM` only with a trusted clock; otherwise `--:--`. Show offline status if Wi-Fi lost, while continuing readings.
- **Absolutely no touchscreen or LCD navigation**. Only BOOT long-press is a physical control, reserved for Wi-Fi setup.
- Implement with ESP-IDF `esp_lcd`, LVGL 9, `esp_lvgl_port`; use guarded buffer memory and avoid needless frame redraws.

## 7. Wi-Fi, provisioning and physical recovery

### 7.1 Normal first-run onboarding

1. With no saved usable Wi-Fi settings, run a password-protected **SoftAP** (example `TempMonitor-A3F2`; SSID suffix derived from device identity). Use unique device-specific setup credentials, not a universal factory password.
2. LCD automatically displays the setup SSID/password or pairing code and URL (`http://192.168.4.1` by default).
3. On phone/laptop, connect to the AP; captive-portal detection may open a landing page. **Manual browser URL must always work** even where captive detection fails.
4. Serve the setup application **entirely from local flash**, without CDNs, external fonts or internet. Scan SSIDs, accept password and timezone/admin setup where applicable, and attempt station association.
5. Validate new Wi-Fi configuration while preserving the old stored credentials until the candidate successfully connects; using AP+STA concurrently during validation is preferred when feasible. Display progress/error and allow retries without trapping the user.
6. Store successful credentials safely in NVS; begin normal STA operation, NTP sync, web service and mDNS if supported. Display IP on a temporary setup/success view or in the web settings, not necessarily on the compact basic LCD.

### 7.2 Established network and failure behavior

- Reconnect with bounded backoff after lost AP; **never stop sensors, LCD or history logging** because Wi-Fi failed.
- Do not erase known credentials or continuously re-enter provisioning on transient Wi-Fi failures.
- Optional name-based access, e.g. `temperature-monitor.local`, uses mDNS where available; IP access remains supported.
- Device communicates using 2.4 GHz Wi-Fi; browser access is local LAN/SoftAP only unless explicitly extended in a future security review.

### 7.3 Using the board's existing BOOT button (GPIO0)

- **While application firmware is already running:** holding BOOT continuously for **5 seconds** (debounced) requests/re-enters password-protected SoftAP provisioning. Short presses do nothing. LCD shows progress, then setup instructions. Continue sensing/logging.
- **During power-on/reset:** BOOT remains an ESP32-S3 **ROM strapping/download-mode** input, not an application command. Holding BOOT while using RESET remains the standard manual flashing escape hatch. No extra GPIO or button is required.
- Never overwrite saved Wi-Fi configuration simply because BOOT was held; only commit after verified replacement settings succeed. Provide a protected **Change Wi-Fi** action in web Settings, too.
- Ensure it is possible to develop/flash over the proper USB port with auto-download or normal BOOT+RESET fallback. Validate against the user's actual dual-USB-C dev board.

## 8. Timekeeping

- After joining Wi-Fi, synchronize via **SNTP/NTP** (configurable server, initially a suitable public pool). Support a local time server. Capture clock-sync state/quality in both APIs and LCD.
- Store timestamps in **UTC** where trusted; format local time using user-selected timezone, including daylight-saving rules.
- Continue keeping time while powered if temporarily offline, tracking last sync and uncertainty. **After power loss without trusted time, do not invent absolute timestamps**.
- When wall time is unknown, retain sequence/boot-id and monotonic offsets for bounded unsynchronized records; if sync occurs later in the **same boot**, a defined rebase process may recover those records. Otherwise expose them as unsynchronized; never quietly treat uptime as UTC.
- Apply deliberate behavior for NTP jumps, DST transitions, stale time and 5-minute window alignment. All display-cycle timing is monotonic, independent of wall-clock corrections.

## 9. Persistent storage and historical data

### 9.1 Storage layout

The N16R8 has **16 MB flash and 8 MB PSRAM**. The partition sizes below are **targets**, not a flash-ready finalized CSV:

| Purpose | Planning size | Notes |
|---|---:|---|
| OTA application slot A | ~4 MB | ESP-IDF firmware **plus embedded compressed React assets** |
| OTA application slot B | ~4 MB | Alternate bootable application for safe updates |
| History LittleFS | ~4 MB | Five-minute aggregates, bounded event records |
| NVS / OTA data / PHY / partition metadata | Board/IDF appropriate | Defined and aligned in actual partition CSV |
| Remaining | ~3–4 MB | Reserve/headroom or expand history after measuring binary |

Check ESP-IDF bootloader/table offsets, partition alignment, OTA metadata and actual firmware size with a generated map/partition table. The web bundle is served from embedded firmware assets in v1 (no extra web filesystem required), so OTA updates switch web and firmware **atomically**. If image exceeds slots, revise the layout **before** deployment; do not silently sacrifice OTA rollback.

NVS holds small configuration values including Wi-Fi, ROM mappings, threshold settings, LCD timings, timezone and administration state. LittleFS is for append/rotation of history and event files. Design record format and migration explicitly.

### 9.2 Sampling vs logging

| Workflow | Default cadence | Medium |
|---|---|---|
| DS18B20 conversion/read | **5 seconds** | RAM snapshot, LVGL and WebSocket |
| Alert evaluation | Each fresh sample/state change | RAM + separately persisted event transitions |
| Persistent temperature history | **5 minutes** | LittleFS rolling, checksummed binary aggregates |
| LCD Trends redraw | When new data available / view active | Small RAM trend cache |
| Clock resynchronization | Periodic, IDF-managed | Time service |

For every five-minute bucket, store an aggregate per valid recognized sensor: **average, minimum, maximum, valid sample count**, status/partial-data flags, ROM identity (direct or via versioned dictionary), timestamp/clock-quality information. The bucket is marked **partial** if a sensor had missing samples. Missing values create gaps, **not zeroes**, and must remain distinguishable from disabled sensors.

Consider compact integer fixed-point encoding rather than JSON. Do not commit to a 48-byte record without a proven schema: avg/min/max for five probes plus metadata, identity and integrity can take substantially more. Validate retention using actual serialized bytes and LittleFS overhead. A provisional **64–128 bytes per five-sensor bucket** gives roughly **four months to seven months theoretical in 4 MiB**, before safety margin and filesystem overhead; report observed capacity rather than promise a precise retention period.

### 9.3 Rotation, resilience and export

- Use bounded segments (e.g. daily or fixed record count) with headers/versioning and checksums; after recovery scan/truncate only damaged trailing records, never corrupt previous good segments.
- Maintain a free-space reserve and delete oldest **completed segments** when necessary. Log count, oldest/latest timestamps and storage-health status for web UI.
- Keep event logs separately bounded so a bad sensor cannot flood or displace all temperature history. Limit event rate and de-duplicate recurring faults.
- Make logging robust to sudden reset/power loss, time-unsynced records, flash errors and concurrent CSV downloads. Measurements must keep running when history is damaged/full/unavailable.
- CSV export includes timestamp (or unsynchronized metadata), ROM ID/name at the relevant time, avg/min/max, count/flags and units, with bounded memory/streamed pagination.
- Do **not** store every five-second reading in flash. Real-time sparklines may use small **RAM-only** buffers without violating the historical five-minute policy.

## 10. Web application

Detailed UX and API design: [`docs/WEB_UI_DESIGN_SPEC.md`](WEB_UI_DESIGN_SPEC.md). Mockups: [`docs/WEB_UI_MOCKUPS.svg`](WEB_UI_MOCKUPS.svg) and `.png`.

- **React 19 + TypeScript + Vite**; static production bundle executing in browser, served by ESP-IDF `esp_http_server`. No Node.js on the microcontroller.
- Style to **Material 3** principles, optionally using expressive details if lightweight and maintained; do not require Material 3 Expressive. Evaluate a React-compatible maintained component library with appropriate Material 3 theming.
- Five pages: **Dashboard** (live sensor cards/status/sparklines), **History** (five-minute avg/min/max graphs, 1h/24h/7d/30d, multi-series, CSV), **Sensors** (name/identity/enable/calibration/alerts), **Alerts** (active and event history), **Settings** (Wi-Fi, LCD Basic/Extended, dwell times, time zone, units, logging/system/OTA).
- Desktop and phone are the **same responsive application**. Provisioning is a simplified flow and must work with no internet.
- **WebSocket** `GET /api/v1/live` (upgrade) publishes a versioned snapshot after connection and delta/snapshot updates about every 5 s and on events; bounded clients, payloads and queues. **REST** serves snapshots, settings mutations, history queries, sensor registry, events and provisioning.
- React client uses local assets, compressed/hashed bundles, browser cache, lazy-loaded charts/pages where useful, backoff/reconnect and REST resync following WebSocket gaps. UI explicitly marks stale or disconnected connections.
- Performance target: aim for **≤1 MB compressed web assets**, *not a hard assumption*. Actual Vite `dist` bytes, compressed bytes and firmware slot headroom are build gates.
- Sample readings in all drawings are fictitious and not acceptance criteria. Never render a temperature threshold label inconsistent with the actual sensor state.

## 11. API and data contracts

Use a versioned `/api/v1/` namespace. Draft minimal endpoints and adjust only with corresponding updates to the web design spec:

| Interface | Contract |
|---|---|
| `GET /api/v1/status` | System, Wi-Fi, clock, LCD mode, storage and version health |
| `GET /api/v1/sensors` | Ordered discovered/configured ROM sensor snapshots and states |
| `PATCH /api/v1/sensors/{rom_id}` | Rename, enable/disable, calibration/thresholds, ordering |
| `GET /api/v1/history` | Time range, selected ROM IDs, avg/min/max, bounded max points, missing gaps and clock-quality flags |
| `GET /api/v1/alerts` | Active alerts and paged events |
| `GET/PATCH /api/v1/settings` | LCD, timezone, units, history policy and permitted system settings |
| `GET /api/v1/wifi/scan` | Network scan with rate limiting |
| `POST /api/v1/wifi/connect` | Authenticated candidate configuration + asynchronous progress |
| `POST /api/v1/wifi/provisioning` | Authorized return to setup mode |
| `GET /api/v1/history/export` | Stream CSV in bounded chunks |
| `POST /api/v1/ota` | Authenticated firmware update, verified and rollback-capable |
| `WS /api/v1/live` | Authenticated socket; version + monotonic sequence, initial full snapshot and updates |

Define JSON schema/OpenAPI or a single authoritative contract and generate/check TypeScript types. The implemented route and schema index is [`contracts/API.md`](../contracts/API.md); shared schemas are used by the TypeScript response validators. All readings are degrees Celsius in machine payloads; conversion for display is performed at presentation boundaries. Avoid unstable sensor indexes: use ROM IDs in requests/storage and user-configured display order separately.

## 12. Security and administration

This device serves local HTTP and receives Wi-Fi passwords; implement **real** security rather than relying on a hidden SSID or an assumed trusted LAN.

- Setup AP must have a per-device unique WPA2/WPA3-compatible passphrase and a first-use pairing/admin bootstrap strategy. **Exact initial admin enrollment flow** is an implementation design gate: document it before authentication code is considered complete. Avoid universal default credentials.
- Provide a changeable administrator password and sessions; store verifiers securely (salted, deliberately costed KDF, e.g. tuned PBKDF2-HMAC-SHA256 using vetted crypto primitives), not plaintext. Rate-limit login/setup and audit repeated failures without leaking secrets.
- Protect **all** mutating API operations, provisioning/reprovisioning, exports when sensitive, WebSocket upgrades and OTA. Check Host/Origin and use suitable CSRF mitigation for cookie-based sessions. Scope initial setup exceptions narrowly.
- Never expose Wi-Fi passwords, session secrets or admin verifiers in logs, URLs, browser localStorage, CSV exports or OTA diagnostic pages.
- Determine and document the LAN transport threat model: HTTPS with provisioned/accepted certificate is preferable for admin credentials; plain HTTP on an untrusted shared LAN does **not** provide confidentiality even with sessions. No WAN port-forwarding. If local HTTP is retained for v1, document the limitation clearly and require explicit acceptance before shipping.
- OTA is authenticated, integrity-verified, size-bounded, applied to the inactive slot, and must have power-interruption and rollback tests. Signed firmware is preferred where practical; do not silently assert signature verification when only a checksum is implemented.
- Handle input size limits, filenames, JSON validation, UTF-8, WebSocket origin/session validation and session expiration. The captive-portal setup must not become an unauthenticated normal-operation configuration backdoor.

## 13. Availability and fault handling

| Fault / condition | Required response |
|---|---|
| No sensors | `No probes detected`; no fabricated alarm |
| 1–4 sensors | Normal partial occupancy; other slots optional/empty |
| Missing expected ROM ID | Debounced disconnection state, history gap and event |
| All sensors suddenly missing / DQ stuck | Single bus-fault status, retries with backoff, no five-spam event storm |
| CRC/85°C power-up/invalid result | Discard erroneous data, mark stale/error, retry |
| Wi-Fi down | LCD continues; sample/alarms/history continue; retry Wi-Fi |
| Cold boot without NTP | `--:--`; unsynchronized records handled explicitly |
| History full or FS damaged | Evict oldest or degrade gracefully, display logging fault; preserve sensor reads |
| WebSocket client drops | Browser shows stale state and reconnects; snapshot resync |
| OTA power failure | Existing working firmware remains bootable or rollback is triggered |
| BOOT+RESET during development | ROM download mode remains available |

## 14. Development, tests and release gates

**Host/CI:** Compile core logic and web app independently, with automated tests for ROM discovery and reordering, CRC/invalid data, alert hysteresis, five-minute aggregation, unknown timestamps, flash segment rotation, API schema/auth, React empty/partial/fault states, button debounce and timer cycling. Use simulated 1-Wire traffic and a fake clock. Fuzz malformed network payloads and storage records where practical.

**ESP32 hardware:** Flash over both applicable USB paths, verify BOOT+RESET download mode and runtime BOOT 5-second hold, bring up and confirm the LCD backlight circuit, then test 1 through 5 physical probes with one pull-up; disconnect/reconnect probes, short/open a test bus **only under a safe controlled setup**, interrupt power during a history write, and perform a two-slot OTA rollback trial. Test RAM/PSRAM/heap, web clients, Wi-Fi reconnect and LVGL while logging concurrently.

**Release acceptance checklist:**

- [ ] Device boots with **zero** peripherals or one through five sensor probes without crashing; samples every ~5 s with stable performance.
- [ ] One 4.7 kΩ bus pull-up, no duplicate pull-ups; all physical probes discovered by ROM ID and CRC-validated.
- [ ] Read-only LCD Basic/Extended modes and configured 20/10-second cycle work; all sensor errors/warnings clearly visible.
- [ ] First-run SoftAP works offline, protects setup, and transitions to STA; 5-second runtime BOOT recovery works without breaking flashing.
- [ ] Web app loads from ESP32 on phone and desktop, has the five pages, live WebSocket temperatures and meaningful network failure indicators.
- [ ] Five-minute avg/min/max/count history retains and rotates cleanly with gaps, timestamp quality, CSV export and independent immediate alert events.
- [ ] All settings/identity bindings persist over power cycles, and renamed/replaced sensors do not cross-contaminate data.
- [ ] Flash layout and OTA image sizes verified against actual 16 MB hardware; rollback and unplanned power-loss tests pass.
- [ ] Credential/enrollment/session/authentication security and LAN transport limitations have been resolved and documented.
- [ ] Temperature monitoring and LCD survive Wi-Fi, web and history failures; no fabricated timestamps or measurements.
- [ ] Hardware sign-off covers display size/controller, header order, supply, backlight and power budget.

## 15. Repository layout and source documents

```text
temperature-monitor/
├── docs/
│   ├── SPEC.md                            # System requirements (this file)
│   ├── TODO.md                            # Ordered implementation checklist
│   ├── ESP32_S3_TEMPERATURE_MONITOR_WIRING.md
│   ├── esp32_s3_temperature_wiring.svg
│   ├── LCD_UI_DESIGN_SPEC.md
│   ├── LCD_UI_MOCKUPS.svg
│   ├── LCD_UI_MOCKUPS.png
│   ├── WEB_UI_DESIGN_SPEC.md
│   ├── WEB_UI_MOCKUPS.svg
│   └── WEB_UI_MOCKUPS.png
├── firmware/                              # ESP-IDF app + components + tests
├── web/                                   # React 19 / Vite / TypeScript
├── tools/                                 # Asset packing, CSV/data inspectors
└── .github/workflows/                     # Optional build/test automation
```

### Dependencies and references

- Project-specific: [`docs/ESP32_S3_TEMPERATURE_MONITOR_WIRING.md`](ESP32_S3_TEMPERATURE_MONITOR_WIRING.md), [`docs/LCD_UI_DESIGN_SPEC.md`](LCD_UI_DESIGN_SPEC.md), [`docs/WEB_UI_DESIGN_SPEC.md`](WEB_UI_DESIGN_SPEC.md).
- Espressif ESP32-S3 hardware / GPIO restrictions: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html
- Boot strapping and download modes: https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html
- ESP-IDF HTTP/WebSocket server: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/protocols/esp_http_server.html
- DS18B20 datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/DS18B20.pdf

## 16. Remaining verification choices (not blockers to writing this spec)

1. **Hardware:** confirm physical LCD size/controller and BL/BLK wiring. Until then, GPIO15-to-BL is **conditional**, not a guaranteed assembly instruction.
2. **Board variants:** confirm photographed development board USB-port roles, GPIO silkscreen orientation and regulator capacity.
3. **Security:** select/test local admin enrollment and LAN HTTPS/certificate strategy before shipping mutable API endpoints.
4. **Binary format:** finalize history record sizes, capacity and flash partition CSV using measured builds; maintain five-minute-only persistent temperature cadence.
5. **Dependency versions:** **ESP-IDF v6.1 is fixed**; pin its exact release revision/toolchain and compatible LVGL/Material/driver components, verify API compatibility and audit maintenance, licenses and production size.

**Design decisions already settled:** ESP-IDF v6.1, one shared DS18B20 GPIO4 bus, exactly one 4.7 kΩ pull-up, web-only control, LCD Basic/Extended modes with 20s/10s defaults, React 19/TypeScript/Vite/Material 3, five-minute historical aggregates, first-run SoftAP and runtime 5-second BOOT recovery.
