# Five-minute history and CSV formats

Temperature records contain 300-second aggregates, never raw five-second writes.
All binary integers are little endian; temperatures are signed millidegrees C.
CRC-32 uses the reflected IEEE polynomial, initial/final XOR `0xffffffff`.

## Binary schema 2

| Offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 4 | `TMH2` magic |
| 4 | 2 | Schema version 2 |
| 6 | 2 | Total record length, including CRC |
| 8 | 8 | Random boot ID |
| 16 | 8 | Window start in monotonic milliseconds |
| 24 | 8 | Signed UTC epoch seconds; meaningful only when the UTC flag is set |
| 32 | 1 | Number of ROM entries, 0–5 |
| 33 | 1 | UTC present: 0 or 1 |
| 34 | 2 | Reserved, zero |
| 36 | variable | ROM entries |
| final 4 | 4 | CRC of all preceding bytes |

Each entry has ROM ID (8), average/minimum/maximum (three signed 4-byte values),
valid count (2), partial flag (1), reserved zero (1), observed state mask (2),
UTF-8 label length (1), then 0–32 label bytes. The label is the last label observed
within that window. Mask bits 0–8 follow the authoritative enum: normal, high,
low, unassigned, disabled, disconnected, stale, invalid, bus fault. Multiple bits
preserve state changes within a window. A zero-count entry carries no measurement:
its three integer slots are zero placeholders, and partial is true. Consumers
must use count to determine validity; CSV exports these temperatures as empty.

The size is `40 + sum(27 + label_bytes)`, maximum 335 bytes. Schema 1 (`TMH1`)
remains readable, including mixed-version segments. Its missing label/mask stay
unknown; current labels are never retroactively assigned. Writers use schema 2.
Unknown newer schemas, completed-segment corruption and interior corruption are
preserved. Only recoverable active tails are truncated. Older firmware may report
history unavailable on schema 2; do not format to force a downgrade.

## Bounds and retention

The temperature store has a 3 MiB budget in the 4 MiB LittleFS partition, with
32 KiB segments and eviction of oldest completed segments. The remaining space
allows filesystem overhead and a separately bounded event store. Simulation uses
its own 64 KiB directory and NVS namespaces.

With maximum-length labels, records for 1–5 probes occupy 99, 158, 217, 276 and
335 bytes respectively. Ignoring segment rounding, the payload budget forecasts
approximately 110, 69, 50, 40 and 32 days at 288 windows/day. These are capacity
forecasts, not measured retained date ranges or a flash lifetime guarantee.
Host-asserted encoded sizes and floor forecasts are recorded in
[`validation/history-retention-estimate.md`](validation/history-retention-estimate.md).

**Measured on the board (2026-11-12, physical probes).** A CSV export of the
last 30 days (and an empty export of the 30 days before) shows what the store
holds: UTC-stamped records from 2026-11-04 11:10 UTC (oldest) to 2026-11-12
07:35 UTC (newest), 314 records from 71 boots. That is 314 of the 2,262
five-minute windows in the span: the board was reflashed and restarted many
times during development, and a restart loses the window in progress. The
store has not yet reached its budget, so no record has been evicted. Records
written before the clock was synchronised are kept by boot and uptime and are
not in this UTC export.

Record sizes follow the encoding above (40 bytes plus 27 bytes and the name per
probe; the 32-character case is the 59 bytes per probe the host tests assert).
The board's records name their probes `Sensor 1` to `Sensor 4`:

| Probes in the record | Records | Bytes each |
| ---: | ---: | ---: |
| 4 | 272 | 180 |
| 1 | 40 | 75 |
| 2 | 1 | 110 |
| 3 | 1 | 145 |

The byte counts are computed from the exported records with that rule, not read
from the flash filesystem. Forecast for this board when it logs without
interruption: four probes with 8-character names use 180 bytes per window, so
3 MiB holds about 61 days (288 windows a day); five such probes (215 bytes)
about 51 days. Longer names shorten this, down to the 32-day floor for five
32-character names. The measurement method: export with
`GET /api/v1/history/export?from=…&to=…` over consecutive 30-day ranges, group
rows by boot ID and window start, and apply the size rule to each record's
names.

## Recovery and backups

LittleFS auto-format is disabled on mount failure. Firmware initializes the
filesystem only when the entire `history` partition is erased; recoverable
partial tails in the active temperature segment or event journal are truncated.
Completed-record corruption, unknown schemas, and nonblank mount failures are
preserved and reported unavailable. Do not erase or format the partition to
make a damaged history appear healthy.

For a logical temperature backup, export CSV before maintenance. CSV contains
temperature aggregates only; it does not preserve event records, sensor
settings, or exact binary segment boundaries. A complete raw history backup
requires putting the ESP32-S3 in ROM download mode and reading only the history
partition with the matching pinned esptool:

```sh
source ~/.espressif/tools/activate_idf_v6.1.sh
python -m esptool --chip esp32s3 -p PORT read-flash 0x820000 0x400000 history-backup.bin
```

Restore that image only to the same partition layout and offset, after checking
the backup file and accepting that restore replaces all current history:

```sh
python -m esptool --chip esp32s3 -p PORT write-flash 0x820000 history-backup.bin
```

Do not use a full-chip erase or change the partition table before preserving
needed data. A normal full `idf.py flash` updates the firmware boot chain and
OTA metadata but does not write the history partition when the partition layout
is unchanged. Device UI does not provide history backup/restore. Its two
confirmed factory-reset choices both erase NVS, including Wi-Fi credentials,
administrator enrollment, sensor configuration, and device preferences. The
preserve-history choice leaves the `history` partition intact; the erase-history
choice erases that partition too. A committed reset intent is processed at
startup before history workers run. If reset processing fails, the intent and
data are preserved for recovery; ordinary corruption never triggers a reset or
format. The physical USB administrator-credential recovery command removes only
admin enrollment; it preserves Wi-Fi, sensor settings, and history and is not a
factory reset.

OTA writes the inactive 4 MiB application slot and validates the image before
selecting it. The bootloader rollback path remains available until startup
passes its health cycles. An interrupted or rejected upload must not select the
partial image; if network recovery is unavailable, use BOOT+RESET ROM download
mode and the documented ESP-IDF flash command. Preserve the history partition
and use the original partition layout during firmware recovery. Forced rollback
and interrupted-upload recovery still require device acceptance tests.

If Wi-Fi drops or credentials change during an upload, the request read fails
or reaches its deadline; the OTA handler aborts the inactive-slot write and
leaves the current boot partition selected. Restore connectivity through the
device AP or USB setup path, check the reported running slot/version, then retry
the image upload. If power fails after a complete image is selected but before
health confirmation, ESP-IDF's pending-verify bootloader state can return to the
previous slot; startup marks the image valid only after two acquisition and
publish cycles. If history reports unavailable after a reset, do not format the
nonblank partition. Keep sampling on the running firmware, preserve the raw
partition using the backup procedure above, then restore a known-good backup or
use the documented explicit recovery procedure. Firmware recovery must preserve
the history offset and partition table. These code paths are implemented; forced
power/network interruption and rollback still need physical acceptance.

## Queries and CSV

`GET /api/v1/history` bounds a half-open range to 31 days and returns at most
512 time groups with up to five series each. Unknown-time queries select one boot
and uptime; UTC queries exclude records without trusted UTC. Downsampling weights
averages by valid count, preserves extrema, and marks gaps/partial data. Omitted
mixed-boot/overlapping records set `truncated`.

`GET /api/v1/history/export` uses the same range/boot/ROM selectors and rejects
`max_points`: it streams full five-minute records, one ROM per CSV row, rather
than graph reductions. It is public and rate limited: one export per 30 seconds per
client address, and at most two history requests running at once. Filesystem
access is serialized; acquisition continues through its bounded write queue.

Deadlines are measured from when a request holds the history lock (waiting for it
is logged separately): 3 seconds for a chart query, 15 seconds for an export. On
expiry the store stops reading within one 4 KiB chunk or one record and starts no
further segment; a chart answer is then valid and `truncated`. A blocking read or
socket send already in progress is not interrupted (socket send timeout 3 s), so
the bound is not hard real time.

A CSV export is never marked partial: damaged completed segments in the range, the
deadline or a storage failure known before the first row give `503` with the
reason; found after rows were sent, the device closes the connection without the
final HTTP chunk, so the browser reports a failed download. Bytes already sent
cannot be recalled, so discard such a partial file. The chart query for the same
range still answers, with `truncated: true`.

Measured latency of full-store queries on the device is not yet established
(gate H07). Broad queries recorded on the board on 2026-11-12 took 1.43 to 1.93 seconds
([record](validation/device-history-load-2026-11-12.md)), which does not meet the original
"well under one second" objective; that build predates the deadline changes above.

CSV schema 1 columns are `schema_version`, `simulated`, `boot_id`,
`timestamp_quality`, `start_uptime_ms`, `utc_seconds`, `rom_id`, `sensor_name`,
`name_source`, `name_escaped`, `average_c`, `minimum_c`, `maximum_c`, `valid_count`,
`partial`, `state_flags`, `units`. Units are always C; flags are four hex digits.
Unknown UTC/name fields are empty and labelled explicitly. Simulated rows carry
`simulated=1`. Text uses RFC 4180 quoting and UTF-8. Labels beginning with a possible
spreadsheet formula receive an apostrophe and `name_escaped=1`; remove exactly that
prefix to recover the original label. Import ROM/boot columns as text to preserve
all hexadecimal digits. No administrator or network credentials are exported.
