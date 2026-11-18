# Internal RAM dips under load — 2026-11-18

Board evidence: the same ESP32-S3 as `history-worker-stack-2026-11-18.md` (three DS18B20 probes
reading, a fourth saved and disconnected, ST7735 LCD, Wi-Fi at 100.64.64.61). Flashed over USB
without erasing.

## Defect

Under the soak load (two WebSocket viewers, status every 2 s, history every 5 s, alerts every
10 s, a CSV export every 60 s), free internal RAM normally sat at 42–55 KB but dropped for
moments to between 20,612 and 228 bytes, against 10,120 on the old firmware.

## Diagnosis

A diagnostic build (not committed): ESP-IDF heap tracing in leak mode with 6-frame call stacks,
plus a task that grouped the live internal allocations by call stack at each new low. Over a
20-minute run, the large holders at the low points were:

| Holder | Size at the lows |
| --- | --- |
| SPI driver's internal copy of each LCD flush (`setup_priv_desc`, from `panel_io_spi_tx_color`) | 4.6–10 KB |
| lwIP TCP send buffers (`tcp_write` → `pbuf_alloc`) while responses stream | 4.6–13.8 KB |
| Wi-Fi driver buffers (`esf_buf_alloc_dynamic`) | about 3.4 KB |
| Chart query results and registry copies | under 6 KB together |

Each is short-lived; the dips are when they coincide. The LVGL draw buffers were in PSRAM
(`buff_spiram`), so every 40-row flush needed a 10 KB internal DMA copy. In that run the copy
failed once (`Failed to allocate priv TX buffer`), and the LCD lost that update. The same run
also restarted once from a task watchdog inside the heap trace's own `record_free`; that was the
diagnostic tracing, not the firmware.

## Fix

- `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` in `sdkconfig.defaults`: Wi-Fi and lwIP buffers come
  from PSRAM first, falling back to internal RAM.
- `display_runtime.cpp`: the two draw buffers are internal DMA memory (`buff_spiram = false`),
  allocated once, with `draw_height` 10 instead of 40: 2 × 128 × 10 × 2 = 5 KB on the 1.8-inch
  panel, so a flush needs no temporary copy.

## Board results (30 minutes, same load)

| | Old firmware (12 Nov) | Refactor with the stack fix | With this fix |
| --- | --- | --- | --- |
| Crashes / restarts | 0 | 0 | 0 |
| Requests (status, history, alerts, export) | all 200 | all 200 | all 200 (894, 360, 180, 30) |
| Error log lines | — | — | 0 |
| Internal RAM lowest free | 10,120 | 5,536 | 34,216 |
| Internal RAM free, lowest health sample | — | — | 53,591 |
| Internal RAM free at the end | 49,003 | 55,043 | 65,679 |
| Main task lowest free stack | 2,284 | 3,068 | 3,308 |
| `history_http` lowest free stack | not logged | 4,060 | 3,996 |
| Display frames dropped | 0 | 0 | 0 |

The old firmware's and the stack-fix columns come from `tools/device_soak_profile.py` reports
(`.build/soak-before-refactor.json`, `.build/soak-after-fix.json`); this run used the same load
with the full console captured, figures from its health and history log lines.
`device_api_test.py --expect-sensors 4` then passed: 518 checks, the same set as before; the 12
extra are journal pages, as the soaks had added events.

Not checked here: how the LCD looks. A 10-row draw buffer means more, smaller flushes; the
owner's look at the screen is the hardware check.
