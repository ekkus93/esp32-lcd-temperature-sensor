# ESP32-S3 Temperature Monitor — Project Handoff (2026-10-30)

**Status:** Software verification and device API/OTA checks pass; physical
peripheral, rollback, durability and release gates remain open. This is not
release approval.

## Repository and build

- GitHub: [ekkus93/esp32-lcd-temperature-sensor](https://github.com/ekkus93/esp32-lcd-temperature-sensor)
- Canonical backlog: [TODO.md](TODO.md); detailed test evidence: [PROGRESS.md](PROGRESS.md) and [validation/](validation/).
- SDK: ESP-IDF v6.1 at `/home/phil/esp/esp-idf-v6.1`, revision `fff9895c82d744c7237be8847347bdd1b07c6643`, GCC `esp-15.2.0_20251204`.
- Verify locally with `source /home/phil/esp/esp-idf-v6.1/export.sh && ./tools/verify.sh --firmware`.

## Current verification

The connected ESP32-S3 appears at `/dev/ttyACM0`, identifies 16 MiB flash and
8 MiB PSRAM, boots the normal image, mounts healthy history, and reports READY
with zero sensors. The bare-board smoke passed; LCD and temperature probes were
not connected.

The authenticated USB-loopback API suite passed embedded UI delivery, auth and
request security, invalid OTA rejection, radio scan, four WebSocket clients
with fifth-client rejection, history bounds, durable reboot event and safe
restart. A 120-second stalled-client test observed firmware cutoff while all
120 concurrent authenticated status checks passed (maximum 255 ms); captured
health logs retained five-second cadence. T07-10 is complete. See
[device API results](validation/device-api-results.txt) and
[smoke results](validation/device-smoke-2026-10-30.txt).

After the one-line Python test-harness correction that preserves health logs
received during the stalled-client bridge, `./tools/verify.sh --firmware`
passed: 19/19 host suites, 14 Python checks, simulator integration, headless
display, 56 frontend tests, formatting/lint/typecheck/production build, and the
pinned ESP-IDF v6.1 firmware build. The image is `0x1e8aa0` bytes with 52% of
the smallest OTA slot free.

On 2026-10-30, the current 2,001,568-byte firmware image was uploaded through
authenticated station HTTP. The board rebooted from `ota_1`, reconnected with
saved settings, passed startup health validation, and preserved administrator
access. Authenticated status and the bundled HTML/hashed JavaScript both
returned HTTP 200 through the USB bridge. A 35-second smoke passed with healthy
history, zero sensors, five-second health cadence, and no panic. A second
authenticated OTA returned the board to `ota_0`, also valid, with admin access
and saved Wi-Fi intact. A reset before the next candidate's health gate caused
bootloader rollback to `ota_0`; admin/Wi-Fi and the prior event-journal boundary
remained intact. T10-01 and T10-03 and A→B→A/early-reset rollback are verified.
Failed-service rollback, LCD status and populated temperature-history
comparison remain open. See
[OTA validation](validation/device-ota-results.txt).

## Remaining acceptance

The numbered backlog stands at **123 complete, 22 open of 145**. Remaining
tasks and immediate prerequisites are:

| IDs | Needed to finish |
| --- | --- |
| T02-01/02/03/14/15 | Board photos and electrical inspection; verify LCD/backlight and the one-wire pull-up before panel/probe wiring; then test 1–5 probes. |
| T04-01/02/03/14 | Electrically verified attached LCD for color/rotation/offset, backlight, DMA/heap/refresh and readability/soak acceptance. |
| T05-10/13/18 | Phone/client for captive onboarding, wrong-password and offline recovery; physical BOOT/RESET interaction. Station DHCP/NTP, NVS save/reboot, prior-network preservation, and connected-board captive DNS/OS redirects passed. |
| T06-14/15/16 | Populated device history for retention/wear/export measurements and a controlled power interruption. |
| T10-02/05/08/09/10 | Failed-service rollback and LCD update status; populated history preservation; combined traffic/flash/peripheral stress and 24–72-hour soak. |
| T11-01/08 | Reconcile measured hardware behavior, complete release qualification and owner signoff before distributing a release. |

Optional non-v1 features remain a separate list at the bottom of [TODO.md](TODO.md)
and are not release blockers. T11-09 handoff/status reconciliation is complete.
No owner network password was needed for the open test network. Do not connect
the LCD/backlight or probes before the electrical prerequisites recorded under T02 are satisfied.

The history partition was previously erased during authorized board recovery;
old history is gone and the original journal failure cause remains unknown.
The current board has the test-managed administrator on it, stored privately
under ignored `.build/`. Never copy its credentials to reports or Git.

## Resume

1. Run `tools/verify.sh --firmware` and record its result.
2. Inspect current USB availability before device access; the serial open resets
   the board. Keep LCD and probes disconnected unless their wiring checks pass.
3. Continue actionable TODOs; request owner network credentials only when a
   successful association test is next and can be performed safely.
4. Keep tasks unchecked until their stated acceptance checks pass, and do not
   label CI development artifacts as release-qualified.
