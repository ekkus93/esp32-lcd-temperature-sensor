# Required-service failure injection through OTA — 2026-11-04 (T10-02 partial)

Purpose: show on the connected board that a new image whose required startup
service really did not start stays `pending_verify` instead of being marked valid,
and that a reset then rolls back to the previous valid slot.

Setup: the normal ESP-IDF v6.1 firmware (committed source, sensor bus enabled in
the local sdkconfig) ran from `ota_0`; one DS18B20 probe and the ST7735 LCD were
connected and Wi-Fi was up. For each test a throwaway image was built from the
committed source with one temporary change to `firmware/main/main.cpp` that skips
the service's start call so the existing health gate sees it as not started. The
source was restored immediately after each build (`git diff` clean) and the image
was never committed. Each image was uploaded to the spare slot with the
authenticated `POST /api/v1/ota` path, booted automatically into `ota_1`, and was
observed for about 50 seconds (the gate needs two acquisition cycles). The OTA
state was then read from the `otadata` partition with `esptool --after no-reset`,
because opening the USB serial port resets this board and would itself trigger
rollback. A hard reset followed, and API status and `otadata` were checked again.

| Injected failure | Image | During the 50 s (API) | `otadata` while injected image ran | After reset |
| --- | --- | --- | --- | --- |
| Sensor bus not initialized | 2,028,080 bytes, SHA-256 `6342b6ba2c709f493355a64eb873103f9ccf749b31f553e4e1698b46319b110d` | `ota_1` / `pending_verify`; Wi-Fi connected, logging healthy | seq 1 VALID, seq 2 PENDING_VERIFY | API `ota_0` / `valid`; seq 2 ABORTED |
| History worker not started | 2,015,056 bytes, SHA-256 `d5e451f05d764fe79dd71fe6aaab8e4a7ee48a18f0413e28fe431c7f4a97a3ed` | `ota_1` / `pending_verify`; logging `healthy: false` | seq 1 VALID, seq 2 PENDING_VERIFY | API `ota_0` / `valid`, logging healthy; seq 2 ABORTED |
| Wi-Fi stack not started | size and hash not captured | API unreachable (no network) | seq 1 VALID, seq 2 PENDING_VERIFY | API `ota_0` / `valid`, Wi-Fi connected; seq 2 ABORTED |

In every case the image was not marked valid even where other services were
healthy (bus case: Wi-Fi, HTTP, authentication and logging all worked), and the
bootloader restored `ota_0` with the failed slot marked ABORTED. Probe data,
history and settings were preserved and the normal firmware was running at the end.

Not done and limits:
- Authentication-start and HTTP-server-start failures were not tested. Three
  upload attempts of the authentication-failure image stalled after the first
  64 KB (curl: 65,536 bytes sent, then no progress for 105 s and a dropped
  connection); the cause is unknown and was not investigated further. The failed
  attempts left the spare slot partly written, and one `otadata` read afterwards
  showed the second entry erased (0xFFFFFFFF) while seq 1 stayed VALID on
  `ota_0`; the board stayed healthy.
- The failures were injected by skipping each start call in a temporary source
  edit, not by naturally failing hardware or drivers. No serial boot log of an
  injected image was captured, for the reset reason above.
- Physical LCD visibility of the OTA-verifying and rollback states was not
  observed.
- Wi-Fi was not stressed and the sequence ran once per service.
