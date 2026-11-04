# Handoff: LCD and temperature monitor

## Current state

The owner's ESP32-S3 is connected at `/dev/ttyACM0`; its 1.8-inch LCD is
connected, but the temperature probes are not. The owner wants LCD work finished
before connecting probes. Use the existing ESP-IDF v6.1 installation at
`/home/phil/esp/esp-idf-v6.1` and follow `AGENTS.md`.

The connected display is marked ST7735 and its PCB says 128 × 120. A 128 × 160
portrait test pattern fits the visible screen. RGB order with controller RAM
offset x=2, y=1 corrected reversed colors and a stray strip at the right edge;
the owner supplied photos showing all four corner labels and correct RGB bars.
The original ST7789 driver remains selectable. The display uses GPIO12/11/10/9/14
for SCLK/MOSI/CS/DC/RESET; firmware does not control the backlight GPIO.

The owner chose to defer multimeter checks. VCC and BL are connected, but their
destinations, voltage, polarity, backlight current and GPIO15 compatibility are
not confirmed. Do not mark electrical acceptance complete or change that wiring
without establishing the relevant details. See
[LCD validation](validation/lcd-st7735-2026-11-03.md).

The board connects to the open **Revival Hall** Wi-Fi network. New installations
default to `America/Los_Angeles`; existing saved timezones are preserved. The
board's settings reported Los Angeles after a restart and the owner confirmed
the LCD displayed the correct local time after synchronization. Configure the
timezone in the web app's Settings page. Credentials and device setup files are
private under `.build/` and must not be committed or printed.

## Display work just completed

- Corrected ST7735 color order and address gaps; added optional diagnostic
  flush timing. A 30-second pattern smoke passed with healthy storage and Wi-Fi;
  measured transfer timing is recorded in the LCD validation file, with its
  limitations.
- Removed redundant LCD text-color updates. A headless regression verifies an
  unchanged snapshot does not invalidate the display; administrator login then
  completed within the 60-second USB bridge limit (previously it took 93 s).
- Verified the device switched Overview/Trends at roughly five-second intervals
  under temporary settings and restored the owner's original settings. The
  owner saw the Trends page and requested larger graphs.
- Compact Trends now displays one 120 × 75-pixel graph at a time, rotating
  through installed sensors every five seconds of *visible Trends time*. This
  preserves the two normal screen modes. Headless tests cover all five
  simulated sensors, automatic rotation, and no sensors. The larger ST7789
  view still shows five sparklines.

## Verification and remaining work

- The larger-Trends firmware was flashed and passed a 15-second board smoke:
  LCD initialized, history was healthy, no probes were fabricated and no panic
  occurred. Time was still unsynchronized during that short run; an earlier
  run synchronized and the owner confirmed the correct local time.
- The final `./tools/verify.sh --firmware` passed: 24 host tests, 30 Python
  tests, simulator and headless display checks, formatting and lint, 78 web
  tests, web production build, and ESP-IDF v6.1 firmware build. Some earlier
  parallel web runs timed out; their focused and later full reruns passed.
- The larger-graph implementation has only been rendered with simulated host data;
  no real probes or five-minute temperature history were available for a
  physical graph or alarm check.
- LCD tasks still open in `docs/TODO.md` include T02-02/03, T04-02/03/14,
  and the LCD portions of T05-10 and T10-02/05/08. Color/corner bring-up
  T04-01 is recorded complete for the selected ST7735. The electrical checks
  are deferred by the owner; do not infer them from a lit screen. Optical
  refresh, display under sustained load, physical alerts and long soak remain.
- The temporary mode test `.build/lcd-mode-cycle.py` uses a private test admin,
  selects five-second Extended dwell for 90 seconds, then restores previous
  display settings. It and the captured `.build/` logs are local artifacts.
- Once LCD work is complete or genuinely blocked, the owner will connect the
  DS18B20 probes. Then verify the single GPIO4 1-Wire bus, exactly one 4.7 kΩ
  pull-up, actual readings and the remaining probe/history/power-loss/soak
  checks in `docs/TODO.md`. Never enable the physical bus before its wiring is
  confirmed.

The owner asked to stop after the current task and handoff, then commit and push
the files to GitHub. Continue with the remaining items in a later session.
