# Route boundary coverage (review audit A10, gate H04) — 2026-11-13

Host evidence and a firmware build. No request was sent to the board.

## What changed

`HttpApi::handle()` now asks one host-built gate before any gated handler:
`route_access()` (the table), `route_needs_csrf()` (owner changes need the CSRF token),
`session_accepted()` (used by `HttpApi::session()`: a valid token and, when required, a matching CSRF
token) and `api_gate()` (404 / 401 / proceed). The dispatcher returns 404 or 401 itself; behaviour
is unchanged.

## Inventory (A10.1)

27 method/path pairs: 12 open (status, history, export, alerts, sensors, sensor storage, probe scan,
clock, settings GET, auth state, login, enroll) and 15 owner (Wi-Fi status/scan GET and POST,
provisioning, connect, logout, password, OTA, reboot, factory reset, settings PATCH, sensor PATCH and
DELETE, calibration hold PUT and DELETE). `/api/v1/live` is the WebSocket, registered separately; it
is public with an Origin check and four clients. `tools/tests/test_route_inventory.py` checks that
the `/api/v1` paths named in `http_api.cpp` and the tested inventory are the same set.

## Gate coverage (A10.2, partly)

`route_access` (ctest) runs every inventory route through the production gate for an anonymous
caller, a session without the CSRF token and a full session. Open routes proceed for all three.
Owner reads need a session. Every owner change, including each destructive route (factory reset,
reboot, OTA, password, Wi-Fi connect and provisioning, settings, sensor edits and retirement,
calibration hold), is refused without both session and CSRF token.

`test_route_inventory.py` checks that inside `handle()` only the three open auth routes are handled
before `api_gate()`, that every other route follows it, and that the 404 and 401 branches return
before any handler. Moving a reboot handler in front of the gate fails it (mutation check).

## Not covered (left open)

The handlers themselves are not run on the host: `http_api.cpp` needs ESP-IDF's HTTP server and
FreeRTOS, and there is no shim that executes `handle()` with mocked side effects. So "no forbidden
side effect is reached" is shown by the gate tests plus the static order check, not by running
each destructive handler with a mock. Board checks of anonymous refusals were not repeated here:
sending a destructive route to the board, even one expected to be refused, risks the owner's data if
the gate were wrong. A10.2 stays open for a production-dispatch harness.
