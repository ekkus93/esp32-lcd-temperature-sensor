# Board soak profile, 2026-11-12 (R10-10, first run)

Physical ESP32-S3 with four saved DS18B20 probes, firmware at commit `f0f20e4` (installed over the
air, slot `ota_0`). Command:

```sh
python tools/device_soak_profile.py --port /dev/ttyACM0 --minutes 45 --warmup-minutes 5
```

Load over Wi-Fi for 45 minutes: two live WebSocket viewers, `GET /status` every 2 s, a 24-hour
history query every 5 s, alerts every 10 s and a 24-hour CSV export every 60 s. The console's
HEALTH lines were read over USB every 5 s.

| Measure | Result |
| --- | --- |
| Requests | 1342 status, 540 history, 270 alerts, 45 exports: all `200` |
| WebSocket | 2 connections held for the whole run, 1082 frames |
| Data received by the client | 8.6 MB |
| Console faults (panic, abort, watchdog) | none |
| Dropped display frames | 0 |
| Internal RAM free after warm-up | 53.5 KB first, 57.7 KB last; median 57.7 KB in every 5-minute block |
| Internal RAM low-water mark | 8392 bytes; smallest largest-free-block 7680 bytes |
| PSRAM free after warm-up | 8,355,512 to 8,352,808 bytes (-2.7 KB) |
| Main task stack low-water mark | 2220 bytes |

**PSRAM.** Free PSRAM is flat except for steps of about 980 bytes at about 17 and 37 minutes. That
matches the LCD trend cache filling up: one point is added every five minutes and its container
allocates storage four points (about 480 bytes) at a time, held twice (the main cache and the
display snapshot). The cache stops growing at its 24-hour limit of 288 points (at most about
70 KB). This explanation is a calculation, not a heap trace.

**Not measured:** LVGL's own memory pool, the number of flash writes, and Wi-Fi traffic sent by the
device beyond what this client received.

**Open:** a run longer than 24 hours, to show that PSRAM use levels off once the trend cache is
full and that nothing else grows.
