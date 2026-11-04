# LCD Bench Checklist (T02-02, T02-03, T04-02, T04-14)

Hands-on checks that software cannot establish. Record every result in the tables below, then copy them into a dated file under `docs/validation/` (for example `lcd-bench-YYYY-MM-DD.md`). Until a task's pass criteria are met, leave it unchecked in `docs/TODO.md`.

**Needed:** multimeter with a mA range (and continuity), a USB power meter if you have one, a phone camera, a ruler or calipers, a 3.3 V-safe test lead set, and the LCD module's seller page or datasheet if any.

**Rules**

- Unplug USB before changing any wire. Never connect `5V` to a 3.3 V-only pin.
- **GPIO15 stays unwired until Part 2 passes.** The firmware has no backlight GPIO driven today, so the backlight currently comes straight from the supply wire you connected to `BL`.
- Record "not measured" rather than guessing. Do not print or commit credentials.

## Part 1: Wiring and header order (T02-02)

Module header order expected (left to right): `GND VCC SCL SDA RES DC CS BL`.

| Check | How | Expected | Result |
|---|---|---|---|
| Header labels match the order above | Read silkscreen on the module | Same order | |
| `GND` to ESP32 `GND` | Continuity, power off | Beeps | |
| `VCC` goes to `3V3` (J1-1), not `5V` | Continuity from `VCC` to the `3V3` pin; confirm no continuity to `5V` | 3V3 only | |
| `SCL`→GPIO12, `SDA`→GPIO11, `RES`→GPIO14, `DC`→GPIO9, `CS`→GPIO10 | Continuity, one wire at a time | Matches `docs/ESP32_S3_TEMPERATURE_MONITOR_WIRING.md` section 1 | |
| Where the `BL` wire ends today | Trace it; continuity to `3V3`, `5V`, a GPIO, or nothing | Write down the destination | |
| `3V3` to `GND` not shorted | Resistance, power off | Not near 0 Ω | |

## Part 2: Backlight (T02-02, T04-02)

Goal: decide whether GPIO15 can drive `BL` directly, needs a transistor, or should stay unwired.

1. With power off, inspect the `BL` pad. Note any transistor (small 3-lead part near the header, marked like `S8050`, `2N3904`, `SS8050` or `Y1`) or series resistor between `BL` and the LEDs. Photograph both sides of the module.
2. Power the module with USB. Measure, with the multimeter on `GND`:
   - `BL` pin voltage: ______ V
   - `VCC` pin voltage: ______ V (expect about 3.3 V)
3. Backlight polarity: is the screen lit when `BL` is high (about 3.3 V) or when it is low? Tie `BL` to `3V3` only through the existing wire, then to `GND` through a 1 kΩ resistor for a short test. Result: lit when ______.
4. Backlight current: put the multimeter (mA range) in series in the `VCC` line with the backlight lit, then again in series in the `BL` line if it is separately fed.
   - Total module current, backlight on: ______ mA
   - Current drawn through `BL`: ______ mA
5. Decision (an ESP32-S3 GPIO should source well under 20 mA continuously):

| Measured | Decision |
|---|---|
| `BL` current ≤ about 5 mA and logic-level input (an on-board transistor is present) | GPIO15 direct drive is acceptable |
| `BL` current above about 10 mA, or no on-board transistor | Use an external NPN or N-MOSFET driver; do not connect `BL` to GPIO15 directly |
| Polarity is active-low | Firmware polarity must be inverted before enabling |
| Cannot determine | Leave GPIO15 unwired and keep the backlight on the supply |

Pass for T02-02: signal order, supply rating, `BL` voltage, polarity and current are all written down. Pass for T04-02: backlight drive decided and, if wiring changed, rechecked; LCD supply is stable in Part 4.

## Part 3: Panel geometry and controller (T02-03)

| Check | How | Result |
|---|---|---|
| Controller marking | Back of the PCB (expected `ST7735`) | |
| PCB marking | Front (`128*120` today) | |
| Visible area (width × height) | Measure the lit window with calipers, mm | |
| Visible diagonal | Measure, convert to inches | |
| Pixel columns/rows actually lit | Flash the test-pattern build (Part 5, step 1) and count the corner labels and border; use a photo | |
| Corner labels TL/TR/BL/BR all visible, border unbroken | Photo | |
| Colour order | Red, green, blue bars appear in that order | |
| Stray pixels on any edge | Photo | |

The firmware currently uses 128 × 160 portrait with x=2, y=1 address gaps. If the visible area is really 128 × 120, note the cut-off rows so the layout can be corrected.

## Part 4: Supply budget and Wi-Fi TX load (T02-03, T04-02)

1. USB power meter inline (or multimeter on the board's `5V` input) with everything connected, five probes if available.
   - Idle, LCD showing Overview: ______ mA
   - During Wi-Fi association and an HTTP page load: ______ mA peak
2. Compare against the supply: the 3.3 V regulator rating (check the board's regulator marking or seller page): ______ mA. Pass if total 3.3 V load stays below about 70% of the rating.
3. Probe `3V3` against `GND` with the multimeter in min/max mode while the device associates with Wi-Fi and while loading the web UI. Expect 3.3 V ±5% and no dips that reset the board or blank the LCD.
   - `3V3` min: ______ V   max: ______ V
4. Watch the serial log for brownout or reboot lines during step 3 and record the reboot count: ______

## Part 5: Readability and soak (T04-14)

Needs probes connected for the alert and trend checks.

1. Flash a current build (confirm with me before flashing so credentials and history stay safe; do not run `erase-flash`).
2. At the intended viewing distance, check Overview with 1, 3 and 5 probes:
   - Longest sensor name truncates cleanly: yes / no
   - Negative temperature readable: yes / no
   - Warning and fault rows distinguishable without relying on colour: yes / no
   - Contrast and brightness acceptable in room light and near a window: yes / no
3. Trends: the single larger chart rotates every 5 s of visible Trends time and gaps for missing data are visible: yes / no. Is it readable at arm's length: yes / no
4. Trigger an alert (warm a probe past its threshold) and confirm it shows on the LCD: yes / no
5. Soak: leave Extended mode running with five probes, LCD on and Wi-Fi connected for at least 24 hours (the T10-08 soak can be the same run). Record start and end uptime, whether the LCD ever froze, blanked or showed stale values, and the reboot count: ______
6. List any font size, truncation or contrast changes needed: ______

## Reporting back

Paste or send the filled-in tables. I will turn them into a validation record, update the LCD sections of the wiring doc and `docs/TODO.md` only where the pass criteria are met, and run `tools/check-secret-leaks.py` before any commit. Host tests and simulator renders do not establish any of the above.
