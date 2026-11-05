# On-board check of mDNS (2026-11-05)

**Evidence type: physical, from one Linux laptop (Avahi) on the same Wi-Fi network.** Firmware: commit
`4e1a1bb`, plain serial flash with no erase. The board kept its history, Wi-Fi settings and password.
Nothing was tried from Windows, macOS, iOS or Android, and the LCD was not inspected.

| Check | Result |
| --- | --- |
| Saved settings from before the change (`TMD1` record) after the flash | loaded; hostname `temperature-monitor` (migration works on real flash) |
| `getent hosts temperature-monitor.local` | resolves to the board's address |
| `avahi-browse -rtp _http._tcp` | the board is listed as "ESP32 Temperature Monitor", port 80, TXT `path=/` and `model=temperature-monitor` |
| `resolvectl query temperature-monitor.local` | failed ("not found"): systemd-resolved's own mDNS is off on this laptop; Avahi resolved it |
| Owner rename to `garage-test` (no reboot) | `200`; `garage-test.local` resolved after 1.2 s; `GET /status` reported `mdns_hostname: garage-test` |
| After the rename: `Host: garage-test.local` / by IP | `200` / `200` |
| After the rename: `Host: temperature-monitor.local` (old name) / `Host: attacker.invalid` | `403` / `403` |
| Owner session after the rename | still signed in |
| Rename to `Bad_Name` | `400` |
| Another responder claimed `garage-conflict.local` (Avahi on the laptop, unrelated address), then the board was renamed to `garage-conflict` | the board reported `mdns_hostname: garage-conflict-2` after 4.3 s; `garage-conflict-2.local` resolved to the board; both `garage-conflict.local` and `garage-conflict-2.local` were accepted as `Host`; the saved setting stayed `garage-conflict` |
| Restore | renamed back to `temperature-monitor` and the conflicting responder stopped; status and resolution back to normal |

The device configuration was left as it was found (default name, signed out).

## What this does not show

- **Other operating systems:** Windows, macOS/iOS and Android were not tried. Android before version 12 and
  Linux without Avahi or nss-mdns may not resolve `.local` names at all; Windows needs a recent Windows 10
  or 11. Use the device's IP address if a client cannot resolve the name.
- **Two physical devices:** the conflict was caused by a second responder on a laptop, not a second board.
- **LCD:** whether the setup screen shows the name on the physical panel, and how it looks, was not seen.
  Headless renders are in `docs/validation/lcd-simulation/provisioning-*local-name*.png`.
