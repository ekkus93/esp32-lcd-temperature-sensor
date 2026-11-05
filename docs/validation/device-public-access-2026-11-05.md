# On-board check of the public read model (2026-11-05)

**Evidence type: physical.** One ESP32-S3 (MAC ending 77:38) with the ST7735 LCD and one DS18B20
probe, flashed with commit `916e758` (plain serial flash, no erase; history, NVS and Wi-Fi settings
kept). All requests came from a computer on the same network through the board's Wi-Fi address.
Nothing here was tried from a phone or a browser, and the LCD was not inspected. No passwords or
tokens are recorded.

## What was checked and the result

| Check | Result |
| --- | --- |
| Anonymous `GET` of status, settings, auth state, sensors, sensors/storage, clock, probe-scan, alerts | all `200` |
| Anonymous `GET /wifi/status` | `401` (owner only) |
| Unknown route `/api/v1/nope` | `404` |
| Anonymous `GET /settings` | display fields only: no `ntp_server`, `etag`, `revision` or `writable` |
| Anonymous `GET /auth/state` | `{"enrolled":true,"signed_in":false}` (no `csrf`) |
| Sensor 4 in the public snapshot | `normal`, 20.4375 C; sensors 1-3 `disconnected` (not plugged in); no bus fault; clock synchronized |
| History query, three in a row | `200` (0.67 s), then `429` with `Retry-After: 1` twice; `200` again after 1.2 s |
| CSV export, twice in a row | first `200` (570 bytes, 0.71 s); second `429` with `Retry-After: 30` |
| WebSocket upgrade, anonymous with the device's Origin | `101`, then a live frame |
| WebSocket with a forged Origin, a missing Origin, a forged Host | each `403` |
| Five viewers connected | all `101`; the 6th, 7th, 8th and 9th viewer `503` |
| REST request while five viewers were connected | `200` |
| Owner login (test-managed account) | `200` after 30.7 s (the password hash) |
| Owner `GET /settings` | includes `ntp_server` and `etag`; `auth/state` shows `signed_in: true` with `csrf` |
| Owner WebSocket upgrade with five viewers already connected | `101` (the reserved sixth slot) |
| `PATCH /settings` and `PATCH /sensors/{rom}` with `{}` and a current `If-Match` | `400` both |
| `PATCH` or `POST /system/reboot` with a session but no CSRF token | `401` |
| After logout: public `GET` still `200`; `PATCH` | `401` |

## What this does not show

- **Not tested:** a phone or any browser, the web app's sign-in screens, the LCD, a wrong-product OTA
  upload, OTA failure injection, a wrong-network Wi-Fi submission, unplugging the bus.
- **Memory:** the free-heap floor with six WebSocket clients connected was not measured (the board
  reports it only on the serial console, and opening the port resets it). The six-client cap and the
  raised socket limit therefore remain unproven for memory; see R07-03.
- **Concurrency:** the check of repeated public history requests not stalling the dashboard was not
  done.
- **Wi-Fi/mDNS:** not part of this pass.
