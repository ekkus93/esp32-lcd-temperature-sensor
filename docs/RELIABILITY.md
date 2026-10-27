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
in the startup console log only. Heap minima, per-socket use, and retained
reset-cause history are not collected; measure them before release as part of
T10-05/08.

## Web bundle measurement

The 2026-10-27 production build reports 637,631 bytes raw and 202,577 bytes
gzip for all 13 embedded assets. The eagerly loaded JavaScript chunks are
587.40 kB raw and 184.29 kB gzip (index plus authentication); lazy routes are
loaded separately. These are build artifact sizes, not measured transfer time,
first-render latency, or device heap/socket cost. Those device measurements
remain open under T09-20 and T10-05.
