# Project handoff — 2026-10-26

## Current state

The required backlog is [TODO.md](TODO.md); 66 named items are checked. This is
an active development build, not a release. Firmware targets an ESP32-S3 with ESP-IDF v6.1; the web app is
React, TypeScript and Vite. Core acquisition, protected local HTTP, Wi-Fi setup,
settings, a simulated five-probe provider, bounded temperature history, and a
headless LVGL renderer are implemented. The physical LCD and sensors have not
been connected or validated. Keep their outputs disabled; review GPIO15 and the
electrical design before connecting them.

The latest work adds `TMH2` five-minute records with per-ROM label and state
metadata, legacy `TMH1` read support, and authenticated streaming CSV export.
The History page offers a five-minute CSV download. See
[HISTORY_FORMAT.md](HISTORY_FORMAT.md) for formats and retention estimates.
Earlier simulated ESP32 checks passed CSV authentication, unknown-clock and
simulation markers, recorded labels, flags, and query rejection; see
[device-history-csv-results.txt](validation/device-history-csv-results.txt).
Those are simulated readings on real hardware, not physical-probe results.

The ESP32 is currently unavailable. The last known flash was the simulation
image; an interrupted attempt to restore the normal image has no surviving
completion log. **Treat the flashed image as unknown** until it can be queried
or reflashed. No device verification is possible at this handoff. Previous
normal-image API and captive-network checks are recorded under `docs/validation/`.

## Reproduce and resume

The pinned SDK is `/home/phil/esp/esp-idf-v6.1`, revision
`fff9895c82d744c7237be8847347bdd1b07c6643` with GCC
`esp-15.2.0_20251204`. Run:

```sh
source /home/phil/esp/esp-idf-v6.1/export.sh
tools/verify.sh --firmware
```

This runs host core/storage/CSV tests, headless LCD tests, formatting, web lint,
unit tests, typecheck/build, SDK pin verification and firmware build. Use
`docs/DEVELOPMENT.md` for separate simulation and device-test commands. When the
board returns, identify its serial port before flashing. Restore the normal
image from `firmware/build` after any simulation tests, then run
`tools/device_smoke.py` and `tools/device_api_test.py`. Do not confuse
`firmware/build-sim` with the normal build. Never print or commit ignored
`.build/` administrator or Wi-Fi secrets.

## Remaining work

- `T06-14` through `T06-16`: retention and recovery measurements, simultaneous
  export and reboot checks, and controlled power-interruption verification.
  `T06-03` storage-failure handling and `T06-10` durable event history also remain.
- `T09-05` and `T09-06`: historical ROM selection, including retired probes,
  phone-friendly chart pan/zoom and bundle work. The current charts render only
  ROMs in the live snapshot. Alert history and pagination (`T07-06`, `T09-10`)
  are unfinished. `T07-05` also remains open because absent entire history
  windows lack explicit gap markers in the HTTP contract.
- Wi-Fi association, DHCP success, NTP, real phone onboarding and BOOT recovery
  need owner network credentials. The owner has not supplied a credential-file
  path. Request an absolute path to a private JSON file with `ssid` and
  `password`; never include its contents in reports or Git.
- OTA service, signed update policy, rollback/recovery, physical sensor/LCD
  integration, fault injection, security and release gates remain open in
  [TODO.md](TODO.md). Keep optional backlog items separate from required work.

The owner selected local HTTP and accepted its documented LAN confidentiality
limitation. The owner explicitly asked to stop after this handoff, so resume the
remaining TODO work only on a later request.
