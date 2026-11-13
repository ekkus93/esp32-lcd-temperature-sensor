# Host retention and eviction profile (review audit A17.1, gate H07) — 2026-11-13

Host fixture evidence only: a POSIX directory with the real `HistoryStore` code, not LittleFS on the
board. Device retention, rollover and latency (A17.2, A17.3) are not measured here.

## Method

`history_retention_profile` (opt-in host tool, `.build/host/history_retention_profile`) fills a
3 MiB budget in 32 KiB segments with five-minute records, writing about 1,000 records past capacity
so steady-state eviction runs. Each probe carries a 32-character name, the worst case for record
size. It now also asserts that what remains is one unbroken run of five-minute records ending with
the last record written, and that the store stays within its budget. Run on source `ae7f41e` plus
this change; 7 min 3 s wall time.

## Results

| Probes | Encoded record (bytes) | Retained file bytes | Records | Oldest start (uptime ms) | Newest start (uptime ms) | Retained days |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 99 | 3,114,045 | 31,455 | 396,000,000 | 9,832,200,000 | 109 |
| 2 | 158 | 3,140,092 | 19,874 | 310,500,000 | 6,272,400,000 | 69 |
| 3 | 217 | 3,133,263 | 14,439 | 317,100,000 | 4,648,500,000 | 50 |
| 4 | 276 | 3,128,460 | 11,335 | 318,600,000 | 3,718,800,000 | 39 |
| 5 | 335 | 3,123,205 | 9,323 | 320,100,000 | 3,116,700,000 | 32 |

In every case the newest retained record is the last one written, and newest − oldest equals
(records − 1) × 5 minutes: eviction dropped only the oldest whole segments.

These are forecasts for the device: LittleFS block overhead, shorter names and real gaps change the
figures. Measured board retention (oldest/newest after a real rollover) remains A17.2.
