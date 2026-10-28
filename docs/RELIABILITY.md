# Reliability and Diagnostics

## Security and safe defaults

The owner selected local HTTP and accepted its LAN confidentiality limit. HTTP
sessions, CSRF, Origin and Host checks protect application actions, but HTTP
and WebSocket traffic is not encrypted. Do not port-forward the device. The
firmware does not log Wi-Fi passwords or administrator secrets.

NVS initialization and settings decode failures are logged and do not trigger an
automatic erase. Sensor defaults remain in RAM when settings cannot be loaded;
settings writes are not accepted when storage is unavailable. LittleFS mounts
with `format_if_mount_failed = false`; auto-format is attempted only when the
entire history partition is erased. Nonblank mount/recovery failures preserve
the partition and report history unavailable. Sensor sampling continues when
history writes fail. The LCD/backlight and 1-Wire bus remain disabled in the
bare-board configuration.

## Watchdogs and reset diagnostics

The checked-in ESP-IDF configuration enables the interrupt watchdog at 800 ms
and task watchdog at 5 seconds, monitoring both idle tasks; task-watchdog panic
is disabled. The bootloader watchdog is enabled with a 9-second timeout. These
are configured safeguards, not evidence of a sustained-load stability test.

Startup logs the application/IDF version, numeric ESP reset reason, PSRAM size,
internal free heap, NVS, Wi-Fi, bus, LittleFS, and event-journal outcomes.
Periodic health logs report free heap and history health. The API exposes Wi-Fi
outcome, history capacity, firmware slot and OTA state. Reset reason is present
in the startup console log only. Firmware HEALTH logs now report internal/PSRAM free and minimum-free bytes,
the largest free internal block, and main-task minimum stack headroom alongside
total free heap. These counters enable subsequent device soak comparisons, but
per-socket allocation, retained reset-cause history, and sustained-load memory
floors are not yet measured; complete T10-05/08 before release.

## Web bundle and browser measurement

The 2026-10-27 production build reports 639,363 bytes raw and 203,464 bytes
gzip for all 14 embedded assets, including the HTML entry point and lazy routes.
Eager JavaScript (dashboard plus authentication) is 587.40 kB raw and 184.29
kB gzip. This is below the 1 MB compressed budget.

A cold Chromium 156/Linux production-preview run against the five-probe loopback
simulator loaded the dashboard with its simulated authenticated state and live
WebSocket. The browser `load` event was 511 ms; the first `/api/v1/status`
response completed at 763 ms from navigation start. Initial compressed HTML,
CSS, JavaScript, and API transfers totaled 188,551 bytes (186,348 bytes for
HTML/CSS/JavaScript and 2,203 bytes for three initial API responses). The eager
resources decoded to about 590 kB; the API snapshot contained five sensors.
Chromium used 7.4 MB JavaScript heap on first measurement and 6.3 MB after 16
seconds. The page stayed connected without a stale-socket alert beyond the
15-second timeout. No console errors occurred.

The preview proxy is loopback-only and uses fake sensor data. These results
measure the built browser UI and its client-side live socket, not ESP32 HTTP
transfer, firmware heap per socket, multi-client memory, or socket-load impact;
those remain in T10-05 and require device profiling. `T09-20` browser/bundle
measurement is complete; see `tools/verify.sh --firmware` for the embedded asset
size and OTA headroom.
