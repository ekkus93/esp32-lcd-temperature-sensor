# Web UI Design Specification — ESP32-S3 Temperature Monitor

**Document:** `docs/WEB_UI_DESIGN_SPEC.md`  
**Status:** Design baseline / ready for implementation planning  
**Revision:** 1.0 — 2026-10-25  
**Related documents:** `SPEC.md` (system specification), `TODO.md` (implementation tracker), `LCD_UI_DESIGN_SPEC.md`, `ESP32_S3_TEMPERATURE_MONITOR_WIRING.md`  
**Related concept art:** `docs/WEB_UI_MOCKUPS.png` and `docs/WEB_UI_MOCKUPS.svg` (illustrative; this document wins when mockups differ)

## 1. Scope and accepted decisions

Build a **self-hosted, responsive, local-first browser application** for one **ESP32-S3-N16R8** temperature monitor. The device supports **up to five DS18B20 probes on one shared 1-Wire bus (GPIO4)**, a read-only 240 × 320 ST7789 LCD, Wi-Fi station mode, and first-run SoftAP provisioning.

Agreed technical direction:

| Area | Decision |
|---|---|
| Frontend | **React 19**, **TypeScript**, **Vite**; static client-side application, no server-side rendering |
| Design | **Material 3** principles, responsive layout; Expressive details optional, not required |
| Component library | Evaluate a supported React-compatible Material UI implementation; use tree-shaken components and customized M3 theme tokens. **Do not assume Google `@material/web` is fully maintained or a drop-in React/M3 Expressive implementation.** |
| Hosting | All production HTML/JS/CSS/icons served by ESP-IDF HTTP server from flash; **no CDN or cloud runtime dependency** |
| Live data | New sensor measurements about **every 5 seconds**; WebSocket live stream with reconnect and REST recovery |
| Persistent data | **One aggregate bucket every 5 minutes**, with average/minimum/maximum per active sensor; no high-resolution continuous flash logging |
| History | Dedicated flash history storage (LittleFS planned), bounded retention/oldest-first eviction; graph by time range; CSV export |
| Settings | Persistent on-device configuration (NVS), web-only editing |
| LCD | Read-only; **Basic** mode = temperatures + status; **Extended** mode alternates Overview and five Trends sparklines |
| Clock | SNTP/NTP over Wi-Fi, user-configurable timezone; display uncertainty when unsynchronized |
| Network | First-run password-protected SoftAP + captive-portal setup; normal station operation afterward |
| Authentication | Protect settings and administrative actions, including setup/reprovisioning; do not expose configuration to arbitrary LAN visitors |

The website is the **only interactive UI**. Do not add touch controls, navigation buttons, or edit widgets to the LCD.

## 2. Goals and non-goals

**Goals**

- At a glance: clearly show readings, names and health for as many as five sensors, including disconnected/disabled/unassigned states.
- A user can connect to the device in AP mode, select an SSID, enter Wi-Fi credentials, and find the device again on the LAN.
- Real-time updates feel immediate without overwhelming the ESP32's networking stack.
- Historical graphing remains informative with 5-minute summary samples.
- Support any number of installed sensors from 1 through 5 and preserve the association of **64-bit sensor ROM ID → user-assigned label and preferences**.
- Alerts are accessible and understandable without confusing `unassigned`, `disabled`, `missing`, `stale`, and `bus fault`.
- Sensor collection and local logging continue when Wi-Fi, the browser, or the internet is unavailable.

**Non-goals for v1**

- Mandatory account/cloud registration, remote internet access, push notifications, telemetry to third parties, or analytics scripts.
- Server-side React, Node.js on ESP32, dynamic npm dependency downloads from device.
- A touchscreen LCD or device-side settings editor.
- Persisting five-second raw readings or arbitrary-duration high-frequency samples.
- Arbitrarily many sensors; v1 maximum is **five managed ROM IDs**.

## 3. Information architecture and navigation

Normal application routes (SPA navigation; server must fall back to index for app routes):

| Route | Desktop navigation | Mobile navigation | Role |
|---|---|---|---|
| `/` | Dashboard | Dashboard | Five live sensor cards, alert banner, recent small graph, device status |
| `/history` | History | History | Detailed multi-sensor historical charts, timeframe, min/avg/max, export |
| `/sensors` | Sensors | Sensors | Discover, identify, name, enable/disable, calibrate, set thresholds |
| `/alerts` | Alerts | Alerts | Current alerts, acknowledgments if supported, historical events |
| `/settings` | Settings | Settings | Wi-Fi, LCD, time, units, storage/logging, security, firmware |
| `/setup` | Dedicated onboarding view | Dedicated onboarding view | AP-mode Wi-Fi network selection and initial configuration |

**Desktop ≥ 960 CSS px:** persistent left navigation rail/drawer, top app bar, fluid content area. Five cards can appear in one row on wide desktops; collapse to three/two as width decreases. Keep chart directly below cards.  
**Tablet 600–959 px:** compact drawer or rail, responsive 2–3 column cards.  
**Mobile < 600 px:** compact top bar with clear connection state, bottom navigation with Dashboard / History / Sensors / Alerts / Settings, and 1–2 cards per row depending on actual width and accessible text zoom. Do not clip names or threshold warnings. Honor safe areas.

**UI intent:** clean Material 3 cards, elevated/tonal surfaces, rounded corners, generous spacing, crisp tabular numeric readings, accessible status text. Light theme default; dark theme (system preference + selectable override) supported. No excessive motion.

## 4. Dashboard — desktop and mobile

**Content order:**

1. App title / device name; Wi-Fi connected/offline/AP indicator; logging OK/error; last live sample time; local clock with synchronized/unsynchronized state.
2. Nonblocking banner for critical conditions, including `Bus fault`, an expected probe disconnect, abnormal temperature, storage write failure; dismissing a visual notice must not clear an underlying active alarm.
3. Up to five sensor cards, ordered by configured display order. Each card includes short user-assigned label, current °C/°F value, explicit status (`Normal`, `High`, `Low`, `Disconnected`, `Stale`, `Disabled`, `Unassigned`), time since update and compact recent trend when available. Cards open sensor details/configuration without changing sensor behavior inadvertently.
4. A preview history chart, defaults to a 24-hour range if data exist; all detected enabled sensor series can be toggled. Link to full History page.
5. Optional device summary: sensor count, uptime, logging capacity/health, LAN IP and firmware version.

**State semantics:**

| State | UI treatment | Temperature display |
|---|---|---|
| `normal` | Neutral/success card + `Normal` text | Valid last sample only |
| `high` / `low` | Strong red/amber border or tonal surface, up/down alert icon, threshold text | Current validated reading |
| `disconnected` | Warning/fault card, `Disconnected`, last seen | `—` or explicitly `Last: ...`; no stale number shown as current |
| `stale` / `invalid` | `Reading delayed` / `Sensor error`; retry status | Clear stale/unknown marker |
| `disabled` | Muted card, `Disabled` | No current monitoring value required |
| `unassigned` | Neutral empty-state affordance | `No probe assigned` (not an alarm) |
| `bus_fault` | Global bus-error notice plus appropriately marked affected sensors | Never fabricate individual readings or five duplicate disconnect alarms |

The shared 1-Wire bus identifies sensors by immutable **ROM code**, not plug/socket number. Channel order is a **logical user-defined list**. A newly detected probe prompts a nonurgent setup action, not an automatic alert.

**Example labels and readings in art are placeholder data.** Readings and alert thresholds depicted in concept art are not acceptance fixtures.

## 5. History page

- Main interactive time-series graph supports ranges: **1 hour, 24 hours, 7 days, 30 days, available history**, with calendar/custom range as a later enhancement.
- Default selected measure: **average**; toggle **min** or **max**, or display min/max as a shaded band around the average when legible.
- A sensor selector shows/hides individual colored series; each series is also labeled by name and icon/legend, not color alone. Display at most five series.
- The source of **historical** graphs is 5-minute aggregate buckets; the dashboard may animate the current 5-second live reading independently. Do not pretend historical storage has 5-second resolution.
- Tooltip includes local date/time and average/min/max values. A missing bucket, unplugged interval, sensor disable interval or unsynchronized timestamp appears as a **gap**, not an interpolated fabricated reading.
- Stream/paginate/downsample server responses for long ranges. Do not send a year's raw history to the browser in one response. CSV export can stream full selected range from storage.
- Indicate available history start/end and current retention/capacity; show a friendly empty state if no valid history yet.
- Optional line for high/low threshold on selected sensor; don't mix incompatible Celsius and Fahrenheit internally (canonical storage uses °C).

**Historical aggregation contract:** Every 5 minutes, for each identified sensor, persist `avg`, `min`, `max`, `valid_sample_count`, relevant status flags, sensor ID, and **UTC bucket timestamp if trusted**. A partially populated window must be marked as partial; no readings from missing sensors are substituted with 0°C. Separate **event log** records may occur immediately for alerts, disconnections, reboots, logging faults and Wi-Fi changes. Alert timing is *not* limited to five-minute history cadence.

## 6. Sensors page

**List** sorted by logical position (1–5), with current/last status and discovery metadata.

Sensor details editor fields:

| Field | Behavior |
|---|---|
| Name / short LCD label | Required display name; validated max length; short label previews 240 px LCD width |
| ROM ID | Read-only, full 64-bit device identifier |
| Enabled | Disable monitoring/alerts for this probe while retaining identity/config/history |
| Display order | Choose position 1–5 without remapping the physical bus |
| Calibration offset | Signed decimal in °C, applied consistently to live displays, history aggregations and alert comparisons; raw value optionally available for diagnostics |
| High/low thresholds | Optional per-sensor limits; validate `low < high` |
| Alert delay/hysteresis | Per-sensor or global defaults; guard against flapping |
| Disconnect alert | Enabled for previously assigned/expected sensors; debounce consecutive failures |
| Remove/forget | Confirm; distinguish removing mapping from deleting retained history |

Sensor identification flow: when an unknown ROM ID appears, show `New sensor found`. Provide a guided **warm one probe and watch which reading changes** instruction, followed by `Assign name`. Do not claim to know which physical cable is which. Allow a newly inserted probe to be unassigned until configured. Resist duplicate assignments to the same ROM ID.

When fewer than five sensors exist, no default disconnect alert should be generated for unused slots.

## 7. Alerts page

- Group **Active** and **Recent events** (resolved, acknowledged, fault). Provide concise event time, sensor label/ROM suffix, reason, current value, configured limit, recovery status, and optional note.
- High, low, disconnected, bus fault, invalid reading, history storage errors, and unsynchronized clock should have distinct status labels.
- Alerts appear in a dashboard banner/card and corresponding LCD row or status bar. Alert badges count **active actual faults**, not empty positions.
- Configurable debounce and hysteresis; defaults for sensor disconnect can be 3 failed reads. Recovery and clear transitions are logged separately.
- Acknowledgment means an operator has seen an alert; it does **not** suppress acquisition, force-clear an active condition, or mask it on the LCD.
- Visual badges are not phone push notifications. Browser Notification API and remote integrations are deferred/optional and must not be required for core monitoring.

## 8. Settings page

Use distinct, comprehensible sections/cards:

### Wi-Fi and network

- Connected SSID, status, DHCP IP, mDNS hostname and signal strength when available.
- Rescan/change SSID, enter password via protected form, save/apply only after explicit confirmation.
- Show reconnection progress; do not erase known-good Wi-Fi credentials on a transient failure.
- Device may offer a dedicated recovery AP after user-initiated action/defined extended failure, subject to security policy.

### LCD display

- **Display mode:** `Basic` (default, continuous overview) or `Extended` (Overview ↔ Trends only).
- **Overview dwell:** default **20 seconds** in Extended mode, adjustable independently.
- **Trends dwell:** default **10 seconds** in Extended mode, adjustable independently.
- **Trends lookback range:** default **60 minutes** (LCD's five small sparklines); granularity constrained by available 5-minute history; the live measurement remains separate.
- **Units:** Celsius/Fahrenheit, default °C.
- Optional `Show disabled sensors`, LCD label preview and **backlight control only after hardware BL pin capability is verified**. Do not promise software brightness control until confirmed.
- No on-LCD tap/press workflow. Preview with accurate 240×320 portrait mock on web.

### Logging and time

- Measurement interval: **5 seconds** (default; if adjustable, bound by DS18B20 conversion time and bus performance).
- **Historical logging interval: fixed 5 minutes for v1** unless deliberately revised system-wide; do not offer sub-five-minute logging options.
- Expose retained range, storage usage, last successful write, mount/errors, CSV export and a separately confirmed clear-history action.
- SNTP sync state/time of last sync; timezone selection. If never synchronized after power-up, show wall-clock data as unknown and do not mislabel history with invented dates.

### System and security

- Device name/hostname, firmware and UI version, uptime, free heap/PSRAM (diagnostic), OTA firmware update with validation/rollback, reboot.
- Administrator authentication and secure password changes; user-visible session handling and logout.
- Separate confirmation flows for reset Wi-Fi, erase history, and factory reset. Never conflate them.

## 9. First-run Wi-Fi provisioning (AP mode)

**Distinct responsive setup interface**, accessed through the ESP32's secured SoftAP (e.g. `TempMonitor-A3F2`) and a captive-portal landing page, with a reliable manual fallback `http://192.168.4.1` on the setup network. All assets must load with **no internet**.

1. LCD displays SSID, unique setup password or access instructions, and setup IP. It is read-only.
2. User joins the ESP32 AP from their phone/computer.
3. Provisioning web page explains the temporary direct connection; scan and list 2.4 GHz networks, plus manual SSID entry and password reveal toggle.
4. User confirms Wi-Fi credentials; progress screen shows joining/testing without exposing the password in logs, responses or browser address bar.
5. When station connection succeeds, show assigned IP and hostname and instructions to join the home LAN to continue. App can redirect only if client network access permits; provide manual fallback.
6. If connection fails, explain the failure and allow retry; keep AP provisioning reachable with measured/time-bounded security protections and preserve already acquired temperatures.

Provisioning credentials are protected; unauthenticated LAN clients must not reconfigure Wi-Fi after onboarding. Do not rely solely on a predictable AP password. Do not assume every phone automatically launches a captive portal.

## 10. Data transport and proposed API boundary

ESP-IDF handles sensor sampling, alarms, and persistence. React is a pure presentation/client layer. A **versioned, stable contract** is needed before concurrent frontend/firmware implementation. Endpoints below are *proposed*, not already implemented:

| Method/endpoint | Description |
|---|---|
| `GET /api/v1/state` | Full live device/sensor snapshot including latest reading, health, Wi-Fi, logging, time sync and active alerts |
| `GET /api/v1/sensors` | Discovered/assigned ROM IDs and user configuration |
| `PATCH /api/v1/sensors/{rom_id}` | Update one sensor name, enabled, thresholds, calibration, order |
| `GET /api/v1/history?from=&to=&series=&metric=&max_points=` | Bounded timeseries query; aligned 5-minute aggregates with null gaps and requested downsampling |
| `GET /api/v1/history/export?...` | Stream CSV with UTC and timezone metadata |
| `GET /api/v1/alerts` | Active and recent alarm event log |
| `GET/PATCH /api/v1/settings` | Validated display, units, timezone, retention/network settings |
| `GET /api/v1/wifi/scan` | Wi-Fi networks in provisioning/authorized mode |
| `POST /api/v1/wifi/connect` | Authorized credential submission; asynchronous status tracking |
| `GET /api/v1/system` | Firmware, uptime, storage and diagnostic state |
| `WS /api/v1/live` | On connect, a complete snapshot; afterward versioned updates about every 5s plus alert/state changes |

Example minimal live message (illustrative):

```json
{
  "type": "snapshot",
  "version": 1,
  "sequence": 215,
  "clock": {"utc": "2026-10-25T23:35:00Z", "synchronized": true},
  "wifi": {"state": "connected"},
  "logging": {"healthy": true, "interval_seconds": 300},
  "sensors": [
    {"rom_id": "28-000000000001", "name": "Living Room", "enabled": true,
     "state": "normal", "temperature_c": 21.8, "age_seconds": 1},
    {"rom_id": "28-000000000002", "name": "Fridge", "enabled": true,
     "state": "high", "temperature_c": 8.5, "age_seconds": 1}
  ]
}
```

The API is authoritative for alerts, status, thresholds and temperatures; the browser must not independently recompute sensor health from chart values. Use REST fetching after WebSocket reconnect to close event gaps. A client should treat readings as **stale** if they stop arriving within a bounded number of expected cycles. Use `ETag`/cache headers appropriately (hashed static assets can be cached; state endpoints must not serve stale responses).

## 11. Visual system and accessibility

| Category | Proposed implementation |
|---|---|
| Color | Material 3 color roles (primary, on-primary, surface, surface-container, outline, error, tertiary). Status semantic roles layered on top. |
| Visual theme | Light default: blue/indigo primary, neutral off-white surfaces, subtle rounded cards. Dark variant optional; do not assume red is the only alarm cue. |
| Typography | Local/system fonts, tabular numerals; 28–36 px headline temperature on desktop/mobile cards, 14–16 px readable labels. Avoid remote font fetches. |
| Spacing | 4/8 px scale; cards 16–24 px padding where space permits; responsive gaps. |
| Controls | At least 44×44 CSS px touch targets on mobile; visible focus and keyboard operation. |
| Contrast | Target WCAG 2.2 AA: at least 4.5:1 for ordinary text and 3:1 for large text/UI where applicable. |
| Motion | Subtle transitions, `prefers-reduced-motion` respected, no forced flashing alarm animations. |
| Feedback | Loading skeleton, unobtrusive toast, form-level validation, accessible live-region updates for newly active alerts. |
| Internationalization | Dates and units formatted through locale APIs; user-set timezone/°C/°F; English baseline; string resources organized for localization. |

**Material 3 Expressive is optional**. Do not ship a heavier or unmaintained library merely to claim Expressive compliance. Choose a supported React component implementation and verify actual firmware image size.

## 12. Device/network/runtime constraints

- The final React production assets are served by the ESP32 itself; build with Vite into static output. No npm, Node.js, browser renderer or React runtime operates on the microcontroller itself.
- Import components and icons selectively, code-split secondary pages, precompress gzip/brotli only if the embedded server/browser negotiation supports them; avoid heavyweight charting libraries and runtime fonts.
- **Performance target:** aspire to ≤1 MB compressed web assets (budget, **not guaranteed or yet measured**). Measure Vite `dist`, compressed assets, combined firmware image and headroom against each **4 MB OTA slot** before release; adjust partitions if needed.
- Monitor heap and HTTP socket usage with several simultaneous browser clients. Avoid unbounded WebSocket outgoing queues, unnecessary polling and megabyte-sized JSON responses.
- A dashboard connection drop shows a visible `Live connection lost` state while allowing cached page navigation; it must not invent fresh readings. Distinguish **ESP32 Wi-Fi disconnected** from **browser cannot contact device**.
- No cloud endpoints, tracking scripts, default outbound requests for fonts, or internet requirement beyond optional SNTP.
- Frontend must not block the firmware's temperature acquisition, LVGL updates or local history writes.

## 13. Authentication and safety of configuration

- Use a per-device setup secret and a changeable administrator credential/session model; define credential storage and initial pairing precisely in the main security specification before shipping.
- All mutating routes require authentication and CSRF-aware protections appropriate to local HTTP deployments (such as per-session CSRF tokens and origin/host checks); WebSocket upgrades must validate sessions and allowed origins.
- Do not store Wi-Fi passwords in browser localStorage, URLs, debug logs or export files. Avoid secrets in crash reports.
- Enforce input validation, rate limits, HTTP request/body limits, safe HTML handling, and OTA signature/integrity checks. Explicitly warn against exposing device HTTP ports to the public internet.
- In SoftAP mode, carefully scope unauthenticated operations to initial provisioning; after provision, lock down reconfiguration.

## 14. Loading, offline and edge-case behavior

| Scenario | Expected browser behavior |
|---|---|
| No sensors installed | `No sensors discovered` onboarding card; no false disconnect alarms |
| 1–4 installed | Render discovered sensors and optional empty positions; no alarm for unused positions |
| Previously assigned probe missing | `Disconnected`, last-seen metadata; graph gap; event if alert enabled |
| Entire 1-Wire bus shorted/faulty | Global bus-fault banner, per-reading invalid states, no bogus values |
| Sensor returns CRC/communication error | Retry/debounce; stale/invalid clearly marked |
| Wi-Fi disconnected | On-device LCD and logging continue; browser shows disconnection when unreachable |
| WebSocket failure | Retry with jittered backoff; REST snapshot after reconnect; stale badge |
| First boot/no NTP | Time `Not synchronized`; new unsynchronized data not assigned false wall-clock timestamps |
| LittleFS full/write error | Preserve measurements, show `History logging unavailable` and remediation; oldest-first retention normally prevents full condition |
| History empty or partial | Helpful empty state; marked partial buckets; no misleading interpolation |
| Sensor disabled | No active alarms for disabled probe; historical data remains accessible |
| Firmware update | Display progress, lock dangerous overlapping actions, recover/reload after reboot |
| 404/route deep link | SPA fallback to index; unknown API route still returns structured API 404 |

## 15. Suggested code organization

```text
web/
  index.html
  vite.config.ts
  src/
    app/          # router, theme, query state, root providers
    components/   # status badges, sensor card, chart, app shell
    features/
      dashboard/
      history/
      sensors/
      alerts/
      settings/
      provisioning/
    api/          # versioned REST client, WS client, runtime schema checks
    types/        # ROM ID, sensor states, sample buckets, alerts
    utils/        # units, time zones, chart downsampling, labeling
    styles/       # Material 3 tokens / light-dark themes
  public/         # strictly local fonts/icons if needed
firmware/
  ...             # sensor, history, Wi-Fi, UI, REST/WS handlers
```

Keep API types shared through a versioned schema or generated TypeScript definitions where practical. Test without actual hardware by mocking versioned snapshots and recorded connection failures.

## 16. Acceptance criteria

- [ ] On a 360 CSS px-wide phone and ≥1280 CSS px desktop, every primary page is readable with no horizontal overflow at 100% zoom.
- [ ] Dashboard accommodates **1–5** sensors, user labels, disconnected/disabled/unassigned/bus-fault states and visible Wi-Fi/logging status.
- [ ] Live updates arrive about every 5 seconds; failures and reconnections are visible and recover without user reload.
- [ ] Historical persistent samples are **five-minute aggregates only** (avg/min/max + validity); charts preserve gaps and support 1h/24h/7d/30d.
- [ ] Sensor settings are keyed to immutable ROM ID and reflected on both web and LCD; enabling/disabling does not erase history.
- [ ] LCD Basic vs Extended mode, two dwell durations, trends lookback, temperature units and time zone can be configured through web only and survive reboot.
- [ ] First-run AP provisioning and manual `192.168.4.1` fallback work without an internet connection.
- [ ] Authenticated writes, safe handling of Wi-Fi credentials, rate-limited sensitive operations and browser connection security are verified.
- [ ] Alarm transitions appear consistently on dashboard, Alerts page and LCD, including recovery and disconnect debounce.
- [ ] History retention, full-storage recovery, time-unsynced records, lost connections, and reboot recoveries are covered by automated tests.
- [ ] Static application and firmware binaries fit the final partition plan with measured headroom; updates work for both OTA slots.
- [ ] At minimum, keyboard accessibility, contrast, clear textual statuses and reduced-motion operation are tested.

## 17. Implementation and design references

- MUI React 19 peer dependencies / installation: https://mui.com/material-ui/getting-started/installation/
- MUI supported release status: https://mui.com/material-ui/getting-started/support/
- Google Material Web maintenance status: https://github.com/material-components/material-web
- ESP-IDF HTTP server / REST / WebSockets / captive portal reference: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/protocols/esp_http_server.html
- Previous LCD UI decisions: `docs/LCD_UI_DESIGN_SPEC.md`

### Mockup interpretation

The concept PNG is a **visual exploration**, not executable HTML and not validated measurements or exact text metrics. The editable SVG is a separate **diagrammatic rendition** of representative screens. Names, dates, readings, example passwords and threshold values are fake examples. The final implementation must follow the state semantics, security constraints, data cadence, and routes in this document if a visual sample is inconsistent.
