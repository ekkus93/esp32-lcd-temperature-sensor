# Single physical probe bring-up and authenticated load — 2026-11-04

Hardware: the connected ESP32-S3 board with the ST7735 LCD, one DS18B20 probe on
GPIO4 and the owner's wiring. The owner did not confirm whether exactly one
4.7 kΩ pull-up is fitted, so T02-14's wiring criteria are not closed by this
record. Firmware: ESP-IDF v6.1 image built with `CONFIG_MONITOR_SENSOR_BUS=y`
(the default image leaves GPIO4 untouched), flashed with a plain `idf.py flash`.

## Bring-up (T02-14, T02-15 partial)

With the bus disabled every HEALTH record reported `sensors=0 bus=disabled`. After
enabling it, the first boot reported `sensors=1 bus=ready` and ten consecutive
five-second SENSOR_DIAG cycles had `last_probes=1 valid=10 invalid=0
fault_cycles=0`; per-cycle read time was 0.76-1.07 s. Valid CRC on every cycle
indicates a working bus, pull-up and common ground, but not that the pull-up is
the single recommended one. The API reported Sensor 1 (ROM 284d086700000009)
`normal` at 18.0625 °C, later 18.5 °C as the probe warmed; the owner read 65.5 °F
on the LCD, consistent with the API. One probe only: two to five probes, cable
reliability and detach/reinsert behaviour are untested.

## Authenticated load (T04-03, T10-05 partial)

A 215-second capture from reset ran with the real probe. After the clock
synchronized, an authenticated session (test-managed administrator file) drove
four WebSocket clients plus three HTTP workers cycling status, sensors, alerts,
bounded history and CSV export for 120 seconds. Results: 130 HTTP requests all
200 (client p50 1.9-4.2 s, p95 4.3-6.4 s, max 6.9 s, depending on route); the
four WebSocket clients all connected and received 82 frames with no errors.

Device side: 43 HEALTH records at 4,998-5,001 ms cadence (no sampling starvation);
43/43 valid probe reads, maximum read 1.132 s; no watchdog, panic, brownout or
error line and no reset after the first. Internal free heap ranged 71,091-83,135
bytes; the internal minimum watermark fell from 43,072 to 40,744 bytes during
boot and to 39,964 bytes when the authenticated load began (66 s), then did not
change for the remaining ~145 s; the largest internal block stayed 31,744 bytes,
PSRAM free minimum was 8,354,148 bytes and the main-task stack minimum 2,668.

Limits: one probe, 120 seconds, one client machine on the LAN, no concurrent
flash-write stress beyond the normal five-minute history cadence and no soak. This
is host-driven network load and does not establish five-probe, long-soak or
optical results.

## LCD alert appearance (T04-14 partial)

With the probe reading about 18.9 °C, Sensor 1's high threshold was temporarily
set to 15 °C through the authenticated sensor API (persistence 15 s, hysteresis
0.5 °C). After about 21 seconds the API reported state `high`, one active alert
and a logged alert event. The owner reported the LCD row showed "HIGH", that it was
readable without relying on colour alone, and that the red alert colour is
liked. The original settings (no low/high limit, persistence 15 s, hysteresis
0.5 °C, calibration 0) were restored afterwards and the alert cleared.

The owner saw only the Temps (Overview) screen during the test because the
saved display mode is `basic`, which never shows Trends. Trends cycling in
`extended` mode, readability across all screens at the real size and a long
soak are not yet checked.
