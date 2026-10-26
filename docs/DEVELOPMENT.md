# Development

## Reproducible toolchain

Use ESP-IDF **v6.1**, commit `fff9895c82d744c7237be8847347bdd1b07c6643`,
and its GCC `esp-15.2.0_20251204` toolchain. Firmware component versions and
registry hashes are tracked in `firmware/main/idf_component.yml` and
`firmware/dependencies.lock`; do not edit managed component sources.

On Ubuntu, install Git, Python 3.10+ with venv, CMake 3.22+, Ninja, a C/C++
compiler, libusb, and clang-format 18. The local installation is already under
`/home/phil/esp/esp-idf-v6.1`. For a fresh machine:

```bash
git clone --branch v6.1 --depth 1 --recursive --shallow-submodules \
  https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6.1
cd ~/esp/esp-idf-v6.1
./install.sh esp32s3
source export.sh
```

In a new terminal, activate and verify before building:

```bash
source /home/phil/esp/esp-idf-v6.1/export.sh
./tools/check-sdk.sh
npm --prefix web ci
npm --prefix web run build
cd firmware
idf.py build
idf.py fullclean build
idf.py -p /dev/ttyACM0 flash monitor
```

The target is ESP32-S3 with 16 MB flash, octal 8 MB PSRAM and USB Serial/JTAG
console. Missing PSRAM is reported without preventing bare-board diagnostics.
The partition CSV reserves two 4 MiB OTA slots and 4 MiB history; measured
application size must fit **both** slots.

## Web development

Node **24.13.0** and npm **11.6.2** were used for the initial locked build.
All web dependencies are exact versions with `web/package-lock.json`.

```bash
cd web
npm ci
npm run dev
npm run lint
npm run typecheck
npm test
npm run build
```

React 19 with MUI 9 and Emotion provides themed Material 3 colors, rounded
surfaces and responsive navigation. System fonts avoid CDN/font downloads.
MUI supports React 19 ([installation](https://mui.com/material-ui/getting-started/installation/)).
The initial gzip bundle is about 179 KB; remeasure as features are added.

The versioned JSON schema is `contracts/snapshot.schema.json`. Regenerate
TypeScript definitions with `npm --prefix web run contract:generate`; runtime
AJV checks reject incompatible snapshots. API and storage schema start at v1;
firmware/UI version starts at 0.1.0. Breaking wire/storage changes require an
explicit version/migration decision.

## Host simulation and checks

```bash
./tools/verify.sh
# Include firmware compilation (requires activated IDF):
./tools/verify.sh --firmware
# Separate terminals for simulated API and browser:
npm --prefix web run simulator -- 5
npm --prefix web run dev
```

The simulator uses the C++ domain model with fake probes/time, binds loopback
only, labels all snapshots as simulated, and provides read-only REST/WebSocket
updates. Use `0` through `5` probes. It does not exercise Wi-Fi, sensors or LCD.
Host tests cover identity/CRC, validation, alert hysteresis, aggregation,
clock quality, BOOT debounce, display cycling and history recovery/rotation.
The storage test accelerates a full day of five-second samples.

## Device tests without peripherals

```bash
python tools/device_smoke.py --port /dev/ttyACM0 --seconds 20
```

Run with the activated IDF Python environment. The test checks repeated health
samples, cadence, zero fabricated probes, heap availability and panic output.
Physical sensor acquisition is disabled by default. Enable it with
`idf.py menuconfig` only after connecting the powered bus and one external
4.7 kΩ pull-up. LCD/backlight GPIOs are not driven by this development build.

For ROM download recovery: hold BOOT, press/release RESET, then release BOOT.
After flashing, release both buttons and tap RESET if the board remains in
ROM download mode. Native USB exposes `/dev/ttyACM0`; the other USB port's
role and manual BOOT long-press behavior still need verification.

## Contribution checks

C/C++ uses clang-format 18 with four-space indentation. TypeScript uses
Prettier and ESLint with strict TypeScript checking. Keep task IDs in TODO;
check tasks only when acceptance criteria pass. Record evidence in
`docs/PROGRESS.md`; distinguish host, device and deferred hardware checks.

## Device setup and authentication tests

```bash
python tools/device_setup.py --port /dev/ttyACM0
python tools/device_api_test.py --port /dev/ttyACM0
python tools/check-secret-leaks.py
```

Setup credentials and the development administrator password are kept only in
mode-0600 files under ignored `.build/`. The API test enrolls a randomly generated
development administrator on an unowned test board. Do not delete the admin
file if you want to keep using that development account. USB bridge responses
may contain sessions: use the test runner's sanitized reports, not raw captures.

`--fresh-enrollment` is an explicit credential-recovery test for accounts marked
as created by this test runner. Ordinary tests preserve existing accounts.
Recovery removes only administrator credentials; sensor settings/history and
Wi-Fi are separate. Login hashing currently takes about 15 seconds on this S3.

The owner explicitly selected local HTTP and accepted LAN confidentiality limits.
The USB bridge exercises the real device HTTP/auth stack through loopback; it
**does not test radio association or a phone's provisioning experience**.
For association tests, provide an owner-created local JSON file with `ssid` and
`password` and use `device_setup.py --wifi-file /absolute/path/to/file.json`.
Passwords are not printed, placed in URLs, or saved in ordinary test logs.

The current firmware embeds the compressed production web build. Rebuilding
`web/dist` updates the generated assets on the next firmware build, including
changes to hashed asset filenames. Generated headers are excluded from source
formatting checks.

## Isolated sensor configuration device tests

`CONFIG_MONITOR_SIMULATION=y` selects five explicit fake probes and an accelerated
clock. It never enables GPIO4 or the LCD. Simulation uses NVS `monitor_sim` and
`/history/simulation`, separate from real configuration/history. Keep its
`sdkconfig` and build directory separate; restore the regular image afterwards.
With the simulation image flashed and a test-managed administrator already
stored privately, run:

```sh
python tools/device_sensor_test.py --port /dev/ttyACM0
# Restart the simulation firmware, then:
python tools/device_sensor_test.py --port /dev/ttyACM0 --verify-reboot
```

These exercise the MCU's actual HTTP, registry, alert and NVS paths with fake
readings; they do not verify physical sensors. Sensor PATCH requests require
CSRF plus the latest sensor `etag` in `If-Match`; expect 202 while queued, 428
without a precondition, 412 for a stale revision, and 409 while another update
is pending. ROM identity and revision fields are read-only. Refresh settings
after the next acquisition cycle before editing again.


## History query contract

`GET /api/v1/history?from=0&to=3600000&boot_id=0123456789abcdef&max_points=120`
selects elapsed milliseconds for one boot. Omit `boot_id` to select trusted UTC
milliseconds; unknown-time records are then excluded. `rom_id` optionally selects
one wire-order ROM. Ranges are half-open, at most 31 days; `max_points` is 1–512
time groups (at most five series per group). Duplicate/unknown query keys fail.
Responses contain count-weighted average, min/max, valid count and partial flag.
Empty intervals remain absent. `truncated=true` reports omitted overlapping or
excess mixed-boot records. No client should silently plot those as complete data.
Queries serialize filesystem access while acquisition continues through its
bounded write queue. CSV export and historical label metadata remain open.

With the simulation firmware running, use
`python tools/device_sensor_test.py --port /dev/ttyACM0 --history-only` to check
persisted LittleFS aggregates and query bounds on the ESP32. This uses explicitly
fake probes and accelerated time, never physical-sensor evidence.

Wi-Fi scanning uses authenticated `POST /api/v1/wifi/scan` (CSRF required), then
polling `GET /api/v1/wifi/scan`. Requests are limited to one per 10 seconds,
results to 20 visible SSIDs, and scan time to 10 seconds. No network is selected
or credential replaced by scanning. Manual entry remains available for hidden
networks. Administrator password changes invalidate all sessions; browser and
USB test bridge allow 60 seconds for the two slow hashes.


## Headless LCD checks and device preferences

Install `libpng-dev` for host framebuffer exports. After ESP-IDF has resolved the
pinned LVGL source, `tools/verify.sh` builds/runs both portable core suites and
`firmware/display_tests`. Software PNGs are written under `.build/display/`;
reviewed fixtures are in `docs/validation/lcd-simulation/`. The firmware compiles
the shared view, but no panel or backlight is initialized. Physical integration
still requires the documented electrical sign-off.

Device preferences use `GET/PATCH /api/v1/settings`, the shared
`contracts/device-settings.schema.json`, and boot/revision `If-Match` tokens.
Their single checksummed/versioned NVS blob is separate from sensor mappings.
Unknown or corrupt stored versions remain preserved and read-only. Simulation
uses the separate `device_sim` namespace. The supported timezone identifiers
map to explicit POSIX DST rules sourced from the installed IANA tzdata; review
those rules when updating dependencies. Unknown identifiers and invalid NTP
hostnames are rejected. Device time remains unknown until an SNTP callback.

Run `device_sensor_test.py --port /dev/ttyACM0 --settings-only` with the simulation
image, restart it, then add `--verify-reboot` to check preference persistence.
The physical LCD is unnecessary. This test selects an example NTP hostname to
exercise reconfiguration, not to claim successful network/time synchronization.
