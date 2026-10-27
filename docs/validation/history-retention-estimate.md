# History record size and capacity forecast

Date: 2026-10-27

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
board is unavailable and has no connected probes in the current setup, so no
oldest/latest device timestamps or real retention range have been measured.
The checked-in design forecast remains useful for planning but is not a device
retention guarantee.
