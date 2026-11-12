# Largest history query and export under load, 2026-11-12 (R02-09)

Physical ESP32-S3, four saved probes. The store held 7.9 days of UTC history (it has not filled; see
`HISTORY_FORMAT.md`), so the largest available request is the 30-day range. Command:

```sh
python3 tools/device_history_load_test.py 100.64.64.61
```

Five 30-day history queries (512 points) and three 30-day CSV exports, while a second client polled
`GET /status` every 0.5 s and a WebSocket viewer received live frames.

| Firmware | 30-day query | 30-day export (128 KB) | Slowest `/status` | Longest live-frame gap |
| --- | --- | --- | --- | --- |
| Before (one chunk per CSV row, on the HTTP task) | 1.54-1.63 s | 6.2 s | 6.18 s | 10.6 s |
| Rows sent in 4 KiB chunks | 1.67-1.79 s | 3.1 s | 3.03 s | 6.5 s |
| Queries and exports on their own worker task | 1.86-1.93 s | 3.3 s | 0.85 s | 5.9 s |

Every request answered `200`; no `/status` request failed. With the worker, the query and export
take slightly longer (the worker runs below the HTTP task's priority) but no longer hold up other
requests: status replies stayed under a second and live frames kept their five-second cadence.
`device_api_test.py --expect-sensors 4` passed all 476 checks on this firmware.
