# Bare-board checks after the R11 and R12 work — 2026-11-12

Physical board, no LCD and no probes attached (so `bus=fault` in the console is expected: the data
line has no pull-up). Firmware built from `54e4789`, flashed over USB without erasing.

## Flash

- Firmware 0.2.0 on `ota_0`, marked `valid` after the restart. Application 0x2136a0 bytes, 48 % of
  each 4 MiB update slot free.
- The board's USB serial port was `/dev/ttyACM1`; `/dev/ttyACM0` was another USB device (a phone).
  The port was chosen by its Espressif USB ID (`303a:1001`) before flashing.

## Settings store (NVS) found full, and repaired

The board had run other firmware since the previous session. On the first boot the monitor logged
`NVS unavailable: ESP_ERR_NVS_NO_FREE_PAGES; no automatic erase`, so Wi-Fi and the sensor settings
did not start. The firmware did not erase anything, as designed.

- A read-only copy of the 24 KiB NVS partition was taken first (kept privately under `.build/`).
- All six pages were marked full; none was empty, and NVS needs one. ESP-IDF's `nvs_tool.py`
  reported the page CRCs as correct and found two orphaned chunks of a blob this project does not
  write (`radio/stations_v2`, 239 entries), plus an old-format password record (`verifier1`).
- Page 0 held no live entries (124 deleted, 2 empty). It was written back blank (0xFF), which gave NVS
  its free page; the store then started with no errors and the remaining live entries were kept.
- Wi-Fi was sent again over USB with `tools/device_setup.py --wifi-file`; the board joined the owner's
  network and answered at `temperature-monitor.local`.
- The old password record did not match the saved owner password and, having no round count, cost
  100,000 PBKDF2 rounds per attempt (about 35 s). It was removed with the USB command
  `RESET_ADMIN_CREDENTIALS CONFIRM`, and the saved password enrolled again after the owner held BOOT
  for five seconds to open the setup window. Sign-in then took 3.3 s (the 10,000-round record), and
  the owner signed in on the web page.

## Event journal

The journal written by the previous build recovered under the new reader (R11-01's change): healthy,
50 events read back from sequence 1, kinds `reboot`, `sensor_state` and `wifi_state`. The LCD trends
were restored from the stored history.

## R12-08 (web on the board)

- The device served `index-Ljds_lHN.js`, the same file as the local build.
- The Roboto Flex font was served by the device with `font/woff2`, and its licence at
  `/licenses/RobotoFlex-OFL.txt` with `text/plain`.
- The owner loaded the pages and signed in. Not checked: a computer with no internet at all, and a
  phone's bottom navigation.

## R11-01 (calibration hold on the board)

| Request | Answer |
| --- | --- |
| `PUT .../calibration-hold` without the device's origin | 403 Invalid host/origin |
| `PUT` with the origin, not signed in | 401 Administrator session required |
| `PUT` signed in, without the CSRF token | 401 |
| `PUT` and `DELETE` signed in, unknown probe | 404 Unknown ROM ID |
| `PUT` on a malformed ROM ID | 404 (no such route) |

Not checked, because no probe was attached or saved: the 409 for a probe without a reading, the
1-to-20-minute range check (the handler checks the connection first), and a hold on a real probe.
