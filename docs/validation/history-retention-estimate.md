# History record size and capacity forecast

Date: 2026-10-31

Host `storage_recovery` tests encode TMH2 records with 32-byte labels for one
through five probes and assert the actual byte lengths. A full 300-second record
is 40 bytes plus 59 bytes per maximum-length probe entry. With 288 windows/day
and the 3 MiB temperature payload budget, the floor forecast is:

| Probes | Encoded record bytes | 3 MiB / record bytes / 288 windows per day |
| ---: | ---: | ---: |
| 1 | 99 | 110 days |
| 2 | 158 | 69 days |
| 3 | 217 | 50 days |
| 4 | 276 | 39 days |
| 5 | 335 | 32 days |

These are host-measured encoded record lengths and a payload-only capacity
calculation. LittleFS segment boundaries, filesystem metadata, shorter labels,
partial windows, and corruption reserve affect actual retained duration. The
checked-in design forecast is not a device retention guarantee.

## Production HistoryStore rollover profile on the host

The opt-in `history_retention_profile` target writes maximum-length five-minute
records through the production `HistoryStore`, exceeds its 3 MiB logical budget,
and measures retained file bytes and oldest/latest boot-relative record starts
after segment eviction. Run it with:

```sh
cmake -S firmware/tests -B .build/host -G Ninja
cmake --build .build/host --target history_retention_profile
.build/host/history_retention_profile
```

The 2026-10-31 POSIX host run reported:

| Probes | Record bytes | Retained file bytes | Records | Oldest start (ms) | Latest start (ms) | Whole retained days |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 99 | 3,114,045 | 31,455 | 396,000,000 | 9,832,200,000 | 109 |
| 2 | 158 | 3,140,092 | 19,874 | 310,500,000 | 6,272,400,000 | 69 |
| 3 | 217 | 3,133,263 | 14,439 | 317,100,000 | 4,648,500,000 | 50 |
| 4 | 276 | 3,128,460 | 11,335 | 318,600,000 | 3,718,800,000 | 39 |
| 5 | 335 | 3,123,205 | 9,323 | 320,100,000 | 3,116,700,000 | 32 |

These are simulated uptime values and POSIX `HistoryStore` file contents, not
real timestamps, LittleFS physical allocation, or probe measurements. They
confirm the host rollover policy retains approximately the payload-only
forecast after segment eviction. Actual MCU LittleFS retained range, filesystem
metadata/wear, and timestamps with physical probes still need device
qualification; T06-15 remains open until those device measurements are
recorded.

## Board measurement (2026-11-12)

Physical ESP32-S3 with four saved DS18B20 probes (`Sensor 1` to `Sensor 4`). CSV export of the last
30 days: 1,133 rows, 314 records from 71 boots, UTC starts 2026-11-04 11:10 to 2026-11-12 07:35
UTC. The export of the 30 days before that was empty. Records: 272 with four probes (180 bytes),
40 with one (75), one with two (110), one with three (145), sizes from the encoding rule applied to
the exported names. The store has not reached its 3 MiB budget, so retention is a forecast: about
61 days at four probes with these names when logging is continuous. The gaps (314 of 2,262 windows)
come from development reflashes and restarts. LittleFS physical allocation was not read from flash.

