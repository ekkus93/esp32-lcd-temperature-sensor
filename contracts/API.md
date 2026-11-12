# `/api/v1` contract

The firmware and browser use JSON Schema Draft 7 contracts in this directory.
Snapshot, settings, sensor, history, alert, network and authentication-state
responses are validated against the shared schemas before typed UI use. API
temperatures are always numeric Celsius; convert to Fahrenheit only for presentation. Sensor
IDs are lowercase 16-character hexadecimal ROM bytes in wire order, never list
indexes. Reads are public and changes need the owner: see `firmware/components/monitor_core/route_access.cpp`
for the single access table. `GET /auth/state` returns `{enrolled, signed_in, csrf?}`; `csrf` appears only
when signed in. No schema version was bumped for `signed_in` or the public settings schema: the only client
is the web app embedded in the same firmware image, so client and device cannot differ. Device settings carry a `hostname` (one lowercase RFC 1123 label, default `temperature-monitor`): it is public, can be changed by the owner, and the device is reachable as `http://<hostname>.local`. The settings record moved from `TMD1` to `TMD2` (a `TMD1` record still loads with the default hostname); as with `signed_in`, no schema version was bumped because the embedded web app is the only client. History queries reject uppercase hex in `rom_id` and `boot_id`, and a `rom_id` given twice, with `400`. Unknown temperatures and UTC are `null`, not zero or fabricated time.
After editing a schema, regenerate checked TypeScript declarations with
`npm --prefix web run contract:generate`.

| Route                                                                                                       | Response contract                                                                                                                                 | Access and notes                                                                                                                                                                                     |
| ----------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `GET /status`, `WS /live`                                                                                   | [`snapshot.schema.json`](snapshot.schema.json)                                                                                                    | Administrator session; WebSocket starts with a complete snapshot.                                                                                                                                    |
| `GET /probe-scan`                                                                                           | [`probe-scan.schema.json`](probe-scan.schema.json)                                                                                                | Administrator session; bounded physical discovery diagnostics. Separate endpoint preserves the strict snapshot v1 contract.                                                                          |
| `GET /clock`                                                                                                | [`clock-diagnostics.schema.json`](clock-diagnostics.schema.json)                                                                                  | Administrator session; last successful SNTP UTC epoch and monotonic age, or explicit `null` before first sync.                                                                                       |
| `GET /sensors`                                                                                              | [`sensor-configurations.schema.json`](sensor-configurations.schema.json)                                                                          | Administrator session; array keyed by `rom_id`, at most five.                                                                                                                                        |
| `GET /sensors/storage`                                                                                      | [`sensor-storage.schema.json`](sensor-storage.schema.json)                                                                                        | Administrator session; reports whether persistent sensor edits are currently accepted.                                                                                                               |
| `PATCH /sensors/{rom_id}`                                                                                   | [`sensor-patch.schema.json`](sensor-patch.schema.json) → `202 {"status":"queued"}`                                                                | Session, CSRF and current `If-Match`; accepted only while sensor storage is writable. Poll sensors for a changed revision and check `/sensors/storage` before claiming durable success.              |
| `DELETE /sensors/{rom_id}`                                                                                  | [`sensor-retire.schema.json`](sensor-retire.schema.json) → `202 {"status":"queued"}`                                                              | Session, CSRF and current `If-Match`; no body; requires disconnected state and a complete scan confirming the ROM is absent. Mapping removal commits before the slot is published free.             |
| `GET /history`                                                                                              | [`history.schema.json`](history.schema.json)                                                                                                      | Session; bounded half-open time range and optional boot/ROM selector. `gaps` names empty intervals between points; `partial` marks gaps inside downsampled points; `truncated` reports omitted data. |
| `GET /history/export`                                                                                       | CSV v1 in [`HISTORY_FORMAT.md`](../docs/HISTORY_FORMAT.md)                                                                                        | Session; streams full five-minute rows; rejects `max_points`.                                                                                                                                        |
| `GET /alerts?after=<sequence>&limit=<1..50>`                                                                | [`alerts.schema.json`](alerts.schema.json)                                                                                                        | Session; active state and cursor-paged events from the bounded persistent journal. Includes journal health; `next_after` is the cursor and `oldest_available` exposes compaction eviction. Each active item has `since_uptime_ms` and `since_utc_seconds` (null until the clock is trusted); the web UI is embedded in the same image, so these added fields need no version change.           |
| `GET/PATCH /settings`                                                                                       | Signed-in owner: [`device-settings.schema.json`](device-settings.schema.json); anyone else (GET only): [`device-settings-public.schema.json`](device-settings-public.schema.json), without `etag`, `revision`, `writable` and `ntp_server`; PATCH body [`device-settings-patch.schema.json`](device-settings-patch.schema.json) | Session; PATCH also requires CSRF and current `If-Match`.                                                                                                                                            |
| `GET /wifi/status`                                                                                          | [`wifi-status.schema.json`](wifi-status.schema.json)                                                                                              | Session; exposes connection state, candidate outcome and bounded error text.                                                                                                                         |
| `GET /wifi/scan`                                                                                            | [`wifi-scan.schema.json`](wifi-scan.schema.json)                                                                                                  | Session; at most 20 visible SSIDs. `POST` starts an asynchronous scan and requires CSRF.                                                                                                             |
| `POST /ota`                                                                                                 | `202 {"status":"rebooting"}`                                                                                                                      | Session and CSRF; raw `application/octet-stream` firmware image, bounded by the inactive slot; ESP-IDF validates the image before slot selection.                                                    |
| `POST /system/reboot`                                                                                       | [`system-reboot.schema.json`](system-reboot.schema.json)                                                                                          | Session and CSRF; empty body only; delayed restart preserves stored device data.                                                                                                                     |
| `POST /system/factory-reset`                                                                                | [`system-factory-reset.schema.json`](system-factory-reset.schema.json)                                                                            | Session and CSRF; exact body `{"erase_history":boolean}`; clears NVS settings/credentials, optionally history, then restarts.                                                                        |
| `GET /auth/state`                                                                                           | [`auth-state.schema.json`](auth-state.schema.json)                                                                                                | Public enrollment state; `csrf` is present only for an authenticated session.                                                                                                                        |
| `POST /auth/login`, `/auth/enroll`, `/auth/password`, `/auth/logout`, `/wifi/connect`, `/wifi/provisioning` | JSON request/acknowledgement                                                                                                                      | Session and CSRF for established-account mutations. Enrollment/login follow their one-time setup flow; a refused enrollment answers 403 (setup window closed), 409 (administrator exists), 429 (within five seconds of an attempt), 400 (password not 12-128 characters) or 503, each with its own `error` message. Passwords are request fields only and never query parameters.                                 |

Unknown routes return a structured error. Mutations reject unknown/duplicate
fields, stale preconditions and oversized payloads. The local HTTP deployment
has the LAN confidentiality limitation accepted by the owner; authentication
does not encrypt network traffic.

The snapshot `logging` object includes `healthy`, the fixed 300-second
`interval_seconds`, and `storage_available`, `storage_total_bytes`, and
`storage_used_bytes`. Capacity fields are zero when the filesystem is
unavailable; they describe LittleFS filesystem capacity, not the history
retention budget. The `firmware` object identifies the running app version,
partition label and OTA image state (`pending_verify`, `valid`, `invalid`, or
another bootloader state) for Settings diagnostics.

`GET /probe-scan` reports physical 1-Wire discovery state.
`discovered_count` counts unique valid DS18B20 ROMs observed in the latest scan,
bounded at 16. `complete` is false before the first successful scan, after an
enumeration cap, or after an iterator error; when false, the count may be
partial. `overflow` means more than five valid ROMs were observed. A successful
scan with five or fewer clears the overflow warning. `enabled` is false for
simulation builds or when the physical bus is unavailable.

The history reducer retains at most 512 gap records and five continuity entries
for the active boot. It allocates no more than 512 time groups with five sensor
cells each (2,560 returned points); omitted gaps or points set `truncated`.
CSV export streams original five-minute buckets and does not use this reducer.

`WS /live` has no per-client queue. Every sample is one complete snapshot frame, sent without
blocking; a client whose socket cannot take the whole frame at once is closed and must reconnect,
and the web client does so with backoff and a fresh REST resync. The live stream is public: four clients are admitted (three viewers and one slot kept for a
signed-in owner, so viewers cannot lock the owner out); further clients are refused. The upgrade still
requires the device's own `Origin` and `Host`. Clients are never sent a partial or queued backlog of older frames.

History is public and therefore rate limited per client address: one `GET /history` per second and one
`GET /history/export` per 30 seconds, and at most two history-type requests running at once. A refused
request returns `429` with a `Retry-After` header (seconds) and costs the client nothing.

`POST /ota` checks that the image's application descriptor names this product (`project_name`)
before writing anything to the inactive slot; a different product is refused with `422`. This is a
sanity check, not authentication: images are not signed and secure boot is out of scope, so
anyone with an administrator session can install any image built for this project.

JSON request bodies are limited to 2,048 bytes and five seconds of total receive
time. Incomplete or timed-out connections are closed; the time spent hashing a
password and OTA upload handling has separate limits.

Sensor storage status is `ready` only after a valid load or when no saved
configuration exists. Load or commit failure switches it to `read_only`; the
API rejects subsequent sensor edits. Queued sensor edits are persisted before
the updated revision is published. If commit fails, the runtime registry rolls
back to the last committed configuration.

Retirement is explicit and separate from disabling monitoring. A probe must be
disconnected, absent from the latest complete physical scan, and the client must
submit its current sensor `ETag`; a replacement is admitted only after the
retirement is durable. If a retired ROM later
reappears, it is discovered with neutral defaults when capacity is available.
History and event records keep their original ROM attribution and settings are
never copied to another ROM.
