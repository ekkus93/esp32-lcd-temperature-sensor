# LCD UI Design Specification — ESP32-S3 Temperature Monitor

**Status:** Design baseline for implementation  
**Date:** 2026-10-25  
**Hardware:** ESP32-S3-N16R8 + SPI ST7789 IPS LCD, **240 × 320 pixels, portrait**  
**Interaction model:** **Read-only LCD**; all configuration and control live in the Wi-Fi web application.  
**Related artifacts:** `SPEC.md` (project-wide requirements), `TODO.md` (implementation checklist), `ESP32_S3_TEMPERATURE_MONITOR_WIRING.md` (wiring), `LCD_UI_MOCKUPS.png` (concept art)

> **Hardware verification:** The photographed LCD board appears to be silkscreened **"2.8 IPS 240x320"**, although we have been referring to it as a ~2-inch display. The interface is specified by its known 240 × 320 pixel resolution and designed conservatively for readability on a **2-inch-class display**. Measure the actual visible diagonal before finalizing physical typography. Confirm ST7789 configuration and backlight electrical requirements during bring-up.

## 1. Purpose and design principles

Provide legible, glanceable status for **up to five** DS18B20 sensors sharing one 1-Wire bus (GPIO4). The LCD is a passive dashboard. It must never require touch input, physical navigation buttons, menu selection, or editing values on-device. Sensor labels, enable/disable, graph range, display mode, dwell time, alerts, Wi-Fi and timezone settings are changed exclusively through the web application and persisted on the device.

- Prioritize all five current readings and actionable warnings over decorative elements.
- Maintain **exactly two normal display views**: **Overview** and **Trends**.
- Use explicit text/symbols in addition to color for status; no warning may rely on color alone.
- Keep sensor acquisition, alert evaluation, history logging and screen cycling independent. Both screens read the same authoritative backend state.
- The LCD remains useful without Wi-Fi or correct wall-clock time.

## 2. Modes and state transitions

| Mode | Normal behavior | Default |
|---|---|---|
| **Basic** | Show **Overview** continuously. | **Selected at first boot** |
| **Extended** | Alternate **Overview → Trends → Overview**, automatically. | Available in web settings |

Extended-mode default cycle: **Overview 20 seconds**, **Trends 10 seconds**. Both durations are independently editable through the web UI, suggested bounds **5–300 seconds** each. Changes take effect promptly without a firmware restart. The Overview is the first view at boot and after switching into Extended mode.

No carousel, navigation strip, touch target, or on-screen settings control. A temporary **Wi-Fi provisioning view** is permitted before initial setup; it is a **system state**, not a third user-selectable normal display screen. Brief connectivity notifications can appear inside a reserved status area, without interrupting ongoing measurements.

## 3. Screen A — Overview (Basic view)

**Content:**

1. Header: short product name (`Temp Monitor` or `Temps`), compact Wi-Fi state icon, **local time** (`HH:MM`) when synchronized.
2. Five fixed-order sensor rows, one per discovered or configured slot. Each row: abbreviated sensor label, prominent current temperature with unit (°C/°F), and warning/status icon as applicable.
3. Lower status band: concise connectivity and logging/storage health indicators. Long IP addresses are **not** required in the main view.

**Layout guidance (240 × 320 portrait):**

| Region | Approximate bounds | Notes |
|---|---|---|
| Header | y = 0–37 | Single-line title, Wi-Fi, time |
| Sensor rows | y = 40–274 | Five rows, approximately 46 px each |
| Footer/status | y = 278–319 | Wi-Fi state, logging health, small alarm summary if necessary |

Use a high-contrast near-black background; temperature numerals roughly **23–28 px** with tabular digits, sensor labels **13–17 px**, top/footer labels **12–14 px**. Preserve readable gutters; abbreviate/truncate excessively long names, optionally configured with an LCD-specific short label. Ensure minus signs, decimals, and degree symbol don't collide with labels. Do not horizontally scroll names.

**Sensor states and display rules:**

| State | Temperature field | Indicator | Requirement |
|---|---|---|---|
| Normal connected | Current valid temperature | Normal/neutral or green | Update on successful sample |
| High alarm | Current valid temperature | `▲` or `!` and red | Persistent until cleared; never rely on red alone |
| Low alarm | Current valid temperature | `▼` or `!` and amber/cyan | Persistent until cleared |
| Missing/unassigned | `--.-°` | `—` or `NO SENSOR` | **No alarm** simply because fewer than five sensors are installed |
| Expected sensor disconnected | `--.-°` or stale value clearly marked | Plug/fault icon and `LOST` | Alarm according to configured debounce |
| Disabled | `OFF` | Gray | Option to hide disabled rows; default retains fixed rows |
| Temporarily invalid/stale | `--.-°` or stale clearly marked | `STALE` / `ERR` | Never show unqualified outdated value as current |

The DS18B20 bus uses one GPIO and identifies probes by **unique 64-bit ROM ID**, not physical port. Users map ROM IDs to human-readable names in the web app. If the bus is globally faulty, show a small `BUS FAULT` warning and mark measurements accordingly; do not manufacture five unrelated disconnect notifications.

**Alerts:** Keep the Overview visible. Use a warning glyph and changed row emphasis; optionally show a small nonmodal footer notification. **Do not automatically switch to a third full-screen alert page.**

## 4. Screen B — Trends (Extended mode only)

Show **five stacked compact sparklines**, one for each enabled/visible sensor; these are **read-only summary trends** and not interactive charts. Use the same sensor ordering, names, temperature units, and warning indicators as the Overview.

Per row:
- Sensor name/short label and current value.
- Small scrolling line trend over a common configured period (default **last 60 minutes**).
- Explicit `NO DATA`, `OFF`, or `LOST` state instead of inventing a trend when no valid samples exist.

Use at most the ~240 × 320 canvas; prefer one tiny line chart per sensor rather than detailed axes, legends, toolbars or pan/zoom controls. Provide overall timespan text (e.g., `Past 60 min`) near the header/footer. An alarming sensor remains identified by an icon and text, even if its line color changes. Avoid slow per-frame plot rebuilding; update plot buffers only as history changes.

**History source:** Use a fixed-capacity RAM/downsampled cache for quick redraw and optionally refill from the persistent history store. Sampling and logging intervals are distinct (current plan: measure every 5 seconds, persist five-minute aggregate records (average/minimum/maximum every 300 seconds)). Missing measurements must create **gaps**, not fabricated straight lines. Avoid filesystem reads on every LVGL frame.

## 5. Wi-Fi, clock, and setup state

- **Time source:** SNTP/NTP after Wi-Fi connection, with time zone configured in the web UI; local time respects configured DST rules where applicable.
- **Online + synchronized:** show current local time and connected Wi-Fi icon.
- **Offline after prior synchronization in same power session:** continue using the running system clock and show Wi-Fi offline; retain an indication when time quality becomes questionable.
- **Cold boot without valid wall-clock synchronization:** display `--:--` until time is established. Never present an invented calendar date/time.
- **Wi-Fi is optional for measurement:** loss of connectivity cannot stop temperature updates or history logging.
- **Initial provisioning:** temporarily show SSID, how to connect, and local provisioning URL (`192.168.4.1` by default). If an AP password is needed, show the device-specific provisioned credential appropriately. Web UI performs all setup; LCD has no input.
- **Provisioned but network unavailable:** continue to show Overview with `Wi-Fi offline`; do not force setup mode or clear credentials on transient failures.

## 6. Web-configurable LCD settings

| Key / UI label | Type / suggested default | Notes |
|---|---|---|
| Display mode | `basic` | `basic` or `extended` |
| Overview duration | `20 s` | Active only in Extended mode |
| Trends duration | `10 s` | Active only in Extended mode |
| Trends time range | `60 min` | Suggested choices: 15 min / 1 h / 6 h / 24 h; bound by available history |
| Temperature units | `°C` | `°C` / `°F`; applies to LCD and web UI by configured preference |
| Time zone | User-selected | Store an unambiguous time-zone rule/identifier |
| Sensor name | User-defined | Stable mapping to DS18B20 ROM ID |
| Sensor enable | Enabled for discovered sensors | Disabling suppresses alerts from that sensor |
| Show disabled sensors | Yes | If hidden, compact remaining rows without automatic pagination |
| Brightness/backlight | Fixed until verified | PWM support only after module circuitry is validated |

Display changes persist in NVS and apply without reboot when technically feasible. Alerts/temperature sampling are not paused when a configuration change is applied.

## 7. LVGL and implementation notes

- Target **LVGL 9** integrated through `esp_lvgl_port` and the ESP-IDF `esp_lcd` ST7789 SPI driver.
- Use one shared immutable/renderable snapshot of sensor state; send structured events to an LVGL UI task to prevent cross-thread LVGL access.
- Screens should be persistent LVGL objects (Overview and Trends) to avoid frequent heap allocation on every dwell transition. Use a monotonic timer for dwell scheduling so NTP corrections cannot affect cycling.
- Place fonts and large LVGL buffers in appropriate memory; size DMA-capable transfer buffers according to the LCD driver constraints. Profile RAM usage and refresh rate on the real board.
- Refresh temperature labels on new readings, time at minute boundaries, and graph points on history updates; avoid full-screen redraws at high frequency.
- Maintain accessible equivalents of color states using symbols and/or concise text.
- LCD uses proposed GPIOs: SPI SCLK=GPIO12, MOSI=GPIO11, CS=GPIO10, DC=GPIO9, RST=GPIO14, BL=GPIO15 **only after backlight input electrical validation**. Sensor bus DATA=GPIO4 with one 4.7 kΩ pull-up to 3.3V.

## 8. Acceptance criteria

- [ ] Basic mode shows up to five named sensor readings, warnings, Wi-Fi status, and time at 240 × 320 with legible typography.
- [ ] Extended mode alternates **only** Overview and Trends, using independently configured durations; no manual input required.
- [ ] Trends screen shows five sensor-specific sparklines (or explicit no-data states) for the configured recent history interval.
- [ ] All LCD configuration is web-only; no touch, menu, or hardware navigation dependency.
- [ ] One connected sensor, all five connected sensors, disconnection, disabled sensor, invalid reading, and bus-wide failure are distinguishable.
- [ ] An alert remains visible on both screens, without blocking other temperature values.
- [ ] NTP synchronization supplies local time; offline and never-synchronized states are rendered correctly.
- [ ] Captive portal initial-provisioning instructions are readable, while the device keeps measuring and logging temperatures.
- [ ] Screen render pipeline remains responsive during Wi-Fi reconnect, history rotation, and temperature measurement.
- [ ] Physical hardware review confirms display controller, actual diagonal, power, and backlight wiring before applying GPIO-driven backlight control.

## 9. Mockup notes

See [`LCD_UI_MOCKUPS.png`](LCD_UI_MOCKUPS.png). This is **visual concept art**, not a pixel-perfect LVGL asset or verified representation of actual font metrics. Any buttons, sample labels, IP addresses, passwords, readings, or dates shown are illustrative only. Implementation follows **this specification** when the artwork is ambiguous. In particular, **the LCD is not touch-enabled**, and there are only **two normal screens**.
