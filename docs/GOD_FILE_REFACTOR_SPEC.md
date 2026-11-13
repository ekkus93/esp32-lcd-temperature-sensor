# God file refactor — specification

Status: proposed, 2026-11-13. Tracked in [GOD_FILE_REFACTOR_TODO.md](GOD_FILE_REFACTOR_TODO.md).

## 1. Purpose

Two parts of the firmware have grown into "God" code: one unit that owns many unrelated
responsibilities, is too long to review as a whole, and cannot be tested without the whole device.

| File | Size (2026-11-13, `3c52431`) | Problem |
| --- | --- | --- |
| `firmware/components/http_api/http_api.cpp` | 1,428 lines | `HttpApi::handle()` is 788 lines and contains the handler for every API route, the portal probes and static file serving. The class also owns the login and history workers, sessions, the Host allow-list, the public limiter, the live snapshot and both update queues, and it reaches about a dozen services. No handler runs in a host test. |
| `firmware/main/main.cpp` | 538 lines | `app_main()` is 404 lines: storage and service start-up, then the whole main loop inline (sensor edits, calibration holds, sampling, probe memory writes, alert and network journaling, trend restore, history buckets, settings persistence, API publication, display hand-off and the OTA boot check). |

This refactor splits both into small units with one job each, behind narrow interfaces, so that
each unit can be read alone and tested on the host. It does not change behaviour.

The other large files were reviewed and are not in scope: `monitor_core/history.cpp` (915 lines)
is one storage class whose length comes from integrity checks; `web/src/features/SensorsView.tsx`
(680) is one list and its editor; `web/src/theme/theme.ts` (606) is mostly configuration. Task G00
re-measures the tree against the criteria in §3 before work starts, and adds any file that meets
them.

## 2. Rules

1. **No behaviour change.** Every route keeps its method, path, access rule, status codes, error
   messages, headers (including `Cache-Control`, `Content-Security-Policy`, `Retry-After`,
   `Set-Cookie`), body shape and timing limits. Contracts stay at schema v1; `contracts/*.json` and
   `contracts/API.md` do not change. Storage formats do not change. The LCD output, the sampling
   cadence and the event journal do not change.
2. **Characterize before moving.** Host tests that pin the current behaviour of each route and each
   loop step are written and passing before that code is moved, and pass unchanged after.
3. **One responsibility per unit**, as in §4. A unit depends on narrow interfaces, not on
   `httpd_req_t`, FreeRTOS handles or the ESP-IDF APIs, unless it is the adapter for them.
4. **Mechanical moves are separate from rewrites.** A commit that moves code does not also change
   its logic, so its review is a comparison of the moved text.
5. **Firmware cost stays small.** The application image grows by no more than 16 KiB (it was
   2,182,064 bytes, 48 % of each OTA slot free). The HTTP task stack, the main task stack and the
   worker stacks keep their configured sizes, and their high-water marks on the board do not get
   worse by more than 10 %. No new heap allocation per request unless one existed before.
6. **Concurrency unchanged.** The same tasks exist, with the same priorities and stack sizes; the
   same mutexes protect the same data; nothing that ran on the HTTP task moves to another task or
   back. Workers still release limiter slots on every outcome.
7. Follow `AGENTS.md`, `CLAUDE.md` and the evidence rules of the earlier plans: host, simulator and
   board evidence are labelled separately; flashing the board needs the owner's confirmation.

## 3. Size and shape targets

A unit is done when:

- no `.cpp` file in scope is longer than 400 lines (tests excluded);
- no function is longer than 80 lines, and the request dispatcher and `app_main()` are under 60;
- each file's top comment states its one responsibility, and its dependencies match it (for example
  the settings routes do not include the Wi-Fi service);
- each route handler and each loop step can be run in a host test with fakes.

G00 checks the whole tree with the same numbers (files over 400 lines, functions over 80) and
lists any other candidate with a keep-or-split decision.

## 4. HTTP API design

### 4.1 Layers

```
esp_http_server  ──►  IdfExchange (adapter: the only code using httpd_req_t)
                         │
                         ▼
                   ApiRouter (route table + the access gate from route_access)
                         │
          ┌──────────────┼──────────────┬───────────────┬──────────────┐
          ▼              ▼              ▼               ▼              ▼
    auth routes   sensor routes   settings routes  network routes  system routes
                  (incl. alerts,   (device          (Wi-Fi status,  (status, clock,
                  calibration      settings)        scan, connect,  probe scan, OTA,
                  hold, retire)                     provisioning)   reboot, reset)
                                        history routes (query, export, worker)
                                        static assets and portal probes
```

### 4.2 The exchange interface

`HttpExchange` is an abstract interface for one request and its answer: method, path, query
string, a bounded header read, the client identity, a bounded JSON body read (keeping the current
2,048-byte, five-second limits and their close-on-timeout behaviour), setting status and headers,
sending a body, sending chunks, and starting an asynchronous answer for the workers.
`IdfExchange` implements it over `httpd_req_t` and is the only file that includes
`esp_http_server.h` for request handling. `FakeExchange` (host tests) records everything sent.

The helpers now in the anonymous namespace (`error`, `json`, `body`, `header`, `cookie`,
`client_id`) become functions over `HttpExchange`.

### 4.3 Services seen through interfaces

Handlers reach the device through narrow interfaces, so a host fake can record side effects:

| Interface | Device implementation | Used by |
| --- | --- | --- |
| `AuthPort` | `AuthService` | auth routes, the gate's session check |
| `NetworkPort` | `WifiService` | network routes, auth enroll (provisioning flag) |
| `HistoryPort` | `HistoryService` | history routes |
| `DeviceSettingsPort` | `DeviceSettingsService` (+ hostname apply) | settings routes |
| `SystemPort` | reboot, factory reset, OTA upload, clock diagnostics | system routes |
| `SensorUpdates` | the sensor-update and calibration-hold queues | sensor routes |
| `LiveState` | the published registry snapshot and its generations | sensor, status routes |

Fakes for the dangerous operations (factory reset, reboot, OTA write, Wi-Fi connect and
provisioning, password change, settings and sensor writes, retirement, calibration hold) count calls.

### 4.4 Router and access

The route table lists method, path pattern (exact, `{rom}` or `{rom}/calibration-hold`) and handler.
`route_access()` stays the single access policy; a host test checks that every table entry has an
access rule and every access rule has an entry (this replaces the source scan in
`tools/tests/test_route_inventory.py`, which is then retired or reduced to checking that no handler
code exists outside the route files). The gate (`api_gate`, `route_needs_csrf`,
`session_accepted`) runs in the router before any handler. Origin and Host checks, the auth routes
that run before the gate, and the portal-probe redirect keep their current order.

### 4.5 What stays in `http_api.cpp`

Server start and configuration, URI registration, the WebSocket hand-off to `ws_live`, the worker
tasks and their queues, the public limiter, and `publish()`/`next_sensor_update()`/
`next_calibration_hold()` for the main loop. Target: under 400 lines.

### 4.6 Proposed files

`http_exchange.hpp`, `idf_exchange.cpp`, `api_router.{hpp,cpp}`, `api_responses.{hpp,cpp}`
(error, JSON and body helpers), `live_json.{hpp,cpp}` (snapshot and sensor serialization),
`routes_auth.cpp`, `routes_sensors.cpp`, `routes_settings.cpp`, `routes_network.cpp`,
`routes_system.cpp`, `routes_history.cpp` (with `serve_history`), `static_assets.cpp`. Names may
change; the split by responsibility may not.

## 5. Main loop design

### 5.1 Start-up

`app_main()` becomes a short sequence of named steps in `startup.cpp`: storage (NVS, factory-reset
intent), settings, history, network and clock, authentication and HTTP, USB console and BOOT
service, sampling and display. Each step keeps its current failure behaviour (log and keep running,
never erase). The trend-restore task and the display-refresh task move to their own files with their
structs.

### 5.2 The loop

A `MonitorLoop` object owns the loop state that is now local to `app_main()` (simulated tick, last
event sequence, prior Wi-Fi and clock states, healthy cycles, OTA check flag, trend restore state)
and runs one cycle as ordered steps. The order is part of the behaviour and must not change:

1. take queued sensor edits (at most four) and commit them (`SensorSettingsPersistence`);
2. apply calibration holds;
3. read the probes and record diagnostics;
4. apply the readings to the registry;
5. write at most one probe memory record, never during a bus fault;
6. journal new alert events;
7. journal Wi-Fi and clock changes;
8. start or apply the trend restore;
9. aggregate and submit the five-minute bucket;
10. persist automatic settings changes, then set the API's writable flag;
11. publish to the API (after step 10, so a rolled-back edit and `read_only` arrive together);
12. hand the frame to the display task;
13. run the OTA boot health check;
14. wait for the next cycle at the same cadence.

Each step is a function on `MonitorLoop` that talks to the device through interfaces where it needs
I/O (sampling, history, API, display, Wi-Fi, OTA), so steps 1, 2, 5–7, 9–11 and 13 can be tested on
the host with fakes. The OTA decision itself stays in the existing host-tested `ota_boot_decision()`.

### 5.3 Targets

`app_main()` under 60 lines; `main.cpp` under 200; every step under 80 lines.

## 6. Testing

- **Characterization.** Before moving a route group, its host tests send requests through
  `FakeExchange` into the real handlers with fake ports, and assert status, headers and body for
  success and each refusal. Before splitting the loop, a host test runs `MonitorLoop` cycles with
  fakes and records the order of side effects.
- **Route boundary (closes A10.2 of the review audit).** For every route: anonymous, session
  without CSRF and full session, through the real router; every refused dangerous request leaves
  every fake's call count at zero.
- **Existing tests stay.** All current host, Python, display and web tests pass without edits
  except mechanical include or build-list changes. `history_export_incomplete`,
  `history_json_allocation_faults`, `calibration_hold_request`, `route_access` and the settings
  persistence tests keep testing the same production functions.
- **Board.** After the last firmware change, with the owner's confirmation to flash:
  `tools/device_api_test.py --expect-sensors <n>` passes, a 30-minute run shows no change in RAM,
  stack high-water marks and request latency compared with the current build, and the LCD and
  history logging work. Recorded under `docs/validation/`.

## 7. Done when

- §3 targets are met, measured by a script added in G00 and run in `tools/verify.sh`.
- Every route and loop step has host tests through the new seams; the A10.2 harness exists.
- `./tools/verify.sh --firmware` and exact-commit CI pass; contracts unchanged; image growth within
  §2.5.
- The board check in §6 is recorded, or explicitly deferred by the owner.
