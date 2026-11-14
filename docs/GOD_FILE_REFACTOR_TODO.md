# God file refactor — TODO

Specification: [GOD_FILE_REFACTOR_SPEC.md](GOD_FILE_REFACTOR_SPEC.md). Work on `master`, one task
per commit, `./tools/verify.sh` (with `--firmware` for firmware changes) before each commit. No
behaviour change: contracts, storage formats and the LCD stay as they are. Flash the board only
with the owner's confirmation.

## Phase G0 — Baseline

- [x] **G00 — Measure and confirm scope** (spec §1, §3).
  - [x] **G00.1** Add `tools/check_unit_size.py`: report `.cpp`/`.tsx`/`.ts` files over 400 lines
    and functions over 80 lines (tests and generated files excluded), with an allow-list for
    accepted exceptions. Python tests for it. Report-only at first.
    Done: `tools/check_unit_size.py` (report-only; `--strict` later), 7 tests in
    `tools/tests/test_check_unit_size.py`.
  - [x] **G00.2** Run it; confirm `http_api.cpp` and `main.cpp` as the targets and record a
    keep-or-split decision for every other file it lists (for example `history.cpp`,
    `usb_bridge.cpp`, `SensorsView.tsx`).
    Done: `tools/unit_size_allowlist.txt` (32 entries with reasons); record
    `docs/validation/god-file-scope-2026-11-13.md`.
  - [x] **G00.3** Record the baseline: source SHA, image size, task stack sizes, and (if the owner
    allows a board run) stack high-water marks, free RAM and API latency from
    `device_api_test.py`.
    Done: `docs/validation/god-file-baseline-2026-11-13.md` (build figures; board figures not
    taken, no flash approval).

## Phase G1 — HTTP API seams

- [x] **G01 — Exchange interface** (spec §4.2).
  - [x] **G01.1** Add `HttpExchange` and `FakeExchange` (host). Move `error`, `json`, `body`,
    `header`, `cookie` and `client_id` onto it in `api_responses`, keeping the body limits.
    Done: `http_exchange.hpp`, `api_responses`, `IdfExchange`, `tests/http_fakes/fake_exchange.hpp`,
    ctest `api_responses`; old helpers are wrappers until G01.2. Image +2,320 bytes.
  - [x] **G01.2** Add `IdfExchange` and make `handle()` use the exchange instead of `httpd_req_t`,
    mechanically, with no route moved yet. Host tests for the helpers (body size, timeout,
    non-JSON, headers).
- [x] **G02 — Service ports** (spec §4.3).
  - [x] **G02.1** Define `AuthPort`, `NetworkPort`, `HistoryPort`, `DeviceSettingsPort`,
    `SystemPort`, `SensorUpdates` and `LiveState`; device adapters over the existing services.
    Done: `api_ports.hpp`, `service_adapters`; `HttpApi` implements `SensorUpdates` and
    `LiveState`; plain types moved to `auth_types`, `network_types`, `history_storage_info`,
    `ota_types`. Image 2,188,336 bytes (+6,272).
  - [x] **G02.2** Host fakes that record every call, including the dangerous operations.
- [x] **G03 — Router** (spec §4.4).
  - [x] **G03.1** Add `ApiRouter` with the route table and the gate (`api_gate`,
    `route_needs_csrf`, `session_accepted`) before any handler; keep the Origin/Host checks, the
    pre-gate auth routes and the portal redirect in their current order.
  - [x] **G03.2** Host test: the table and `route_access()` cover the same routes, both ways.
    Done: `handle()`, `serve_history`, the workers, `origin` and `session` use `HttpExchange`;
    only the WebSocket hand-off and the OTA stream touch `httpd_req_t`. Image +3,056 bytes.
    Done: `tests/http_fakes/fake_ports.hpp` (`FakeDevice::side_effects()`), ctest
    `http_fake_ports`.
    Done: `api_router.{hpp,cpp}` (27-route table, `gate_request`), ctest `api_router`; the
    Python inventory also compares the router table with `route_access.cpp`.

## Phase G2 — HTTP API split (characterize, then move)

Each task: write characterization tests through `FakeExchange` for the group against the current
code, then move the group into its file in a separate commit, with the tests unchanged.

- [x] **G04 — Auth routes** (state, enroll, login with its worker, logout, password).
  Done: `routes_auth.{hpp,cpp}` over `RoutePorts`/`RouteHost` (`api_routes.hpp`), ctest
  `routes_auth`. Deviation: the old code cannot run on the host, so the move is text-preserving and
  its tests are written against the moved handlers in the same task (spec §2.2); the board check in
  G19 is the end-to-end comparison. `enrollment_reply` moved to `auth_types.cpp`.
- [x] **G05 — Sensor routes** (sensor list, storage status, alerts, PATCH, retirement, calibration
  hold; uses `calibration_hold_request`).
  Done: `routes_sensors.{hpp,cpp}` and `live_json` (sensor, event and boot-id JSON); ctest
  `routes_sensors`; `SystemPort::trusted_utc()` added for the alert times.

- [x] **G06 — Settings routes** (GET and PATCH settings, hostname apply, owner-only fields).
  Done: `routes_settings.{hpp,cpp}`, ctest `routes_settings`; `SystemPort::configure_clock()` added.
- [x] **G07 — Network routes** (Wi-Fi status, scan GET/POST, connect, provisioning).
  Done: `routes_network.{hpp,cpp}`, ctest `routes_network`.
- [x] **G08 — System routes** (status, clock, probe scan, OTA upload, reboot, factory reset).
  Done: `routes_system.{hpp,cpp}`, ctest `routes_system`.
- [x] **G09 — History routes** (query, export, admission, worker, `serve_history`; keeps
  `export_history_csv` and `build_history_json`).
  Done: `routes_history.{hpp,cpp}` (`route_history`, `serve_history`), ctest `routes_history`.

- [x] **G10 — Static assets and portal probes** (asset lookup, gzip requirement, cache and security
  headers, the SPA fallback list, socket close after an asset).
  Done: `static_assets.{hpp,cpp}` (`serve_static_asset` template over any asset table,
  `answer_portal_probe`), ctest `routes_static`.

- [x] **G11 — Live JSON** (snapshot and sensor serialization into `live_json`).
  Done (before G08, which needs it): `snapshot_json()` joins the sensor, event and boot-id JSON
  moved in G05; ctest `routes_live_json`.
- [x] **G12 — Route boundary harness** (spec §6; review audit A10.2).
  - [x] **G12.1** For every route: anonymous, session without CSRF and full session through the
    real router with fakes; every refused dangerous request leaves every fake untouched.
  - [x] **G12.2** Mark review audit A10.2 done with this evidence; reduce or retire
    `tools/tests/test_route_inventory.py` as the spec says.
    Done: `api_dispatch.{hpp,cpp}` (`dispatch_api`, `handler_for`), ctest `route_boundary`;
    `test_route_inventory.py` reduced to the table/policy match and "no handler in http_api.cpp";
    review audit A10.2 and G1 ticked.

- [x] **G13 — Slim `http_api.cpp`** (spec §4.5): only server lifecycle, registration, workers,
  limiter and the main-loop interface remain; under 400 lines; `handle()`/dispatch under 60.
  Done: 361 lines (from 1,428); `handle()` 13 lines; `HttpApi::start()` split into
  `connect_services`, `start_workers` and `start_server`; image 2,188,336 bytes (+6,272).

## Phase G3 — Main loop split

- [ ] **G14 — Loop characterization** (spec §5.2, §6).
  - [ ] **G14.1** Define the device interfaces the loop needs (sampling, history, API, display,
    Wi-Fi, OTA) with host fakes.
  - [ ] **G14.2** Host test that runs cycles and records the order of side effects, including: edits
    committed before holds and sampling; at most one probe memory write per cycle and none in a bus
    fault; writable flag set before publish; OTA check only after two healthy cycles.
- [ ] **G15 — `MonitorLoop`**: move the loop state and steps out of `app_main()` into
  `monitor_loop.{hpp,cpp}`, one function per step, order unchanged; the G14 test passes unchanged.
- [ ] **G16 — Start-up and tasks**: move start-up into named steps in `startup.cpp`, and the trend
  restore and display refresh tasks into their own files; same failure behaviour.
- [ ] **G17 — Slim `main.cpp`**: `app_main()` under 60 lines, `main.cpp` under 200, every step under
  80 lines.

## Phase G4 — Verification

- [ ] **G18 — Enforce the targets**: make `check_unit_size.py` fail `tools/verify.sh` for files in
  scope that exceed §3, with the G00 allow-list for accepted exceptions.
- [ ] **G19 — Final checks** (spec §7).
  - [ ] **G19.1** `./tools/verify.sh --firmware` and exact-commit CI pass; contracts and generated
    types unchanged; image growth within 16 KiB of the G00 baseline.
  - [ ] **G19.2** With the owner's confirmation, flash and run `device_api_test.py`, a 30-minute
    RAM/stack/latency comparison with the G00 baseline, and check the LCD and logging. Record under
    `docs/validation/`, or record the owner's deferral.
  - [ ] **G19.3** Update `docs/DEVELOPMENT.md` (where HTTP and loop code now lives, how to add a
    route) and note completion in the review audit TODO where A10.2 is affected.

## Completion

- [ ] **Done:** G00–G19 complete; no file in scope over 400 lines and no function over 80; every
  route and loop step tested through the new seams; behaviour, contracts and board results
  unchanged.
