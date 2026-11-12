# ESP32-S3 Temperature Monitor 0.2.0 — release notes

> **Status: prepared, not yet published.** The version and these notes are ready. Publishing needs
> the owner's go-ahead to merge the release branch into `master` (where CI builds the release
> package) and to create the GitHub release. The open checks under **Known issues** are released
> as known issues only with the owner's approval.

0.2.0 is the first numbered release. Earlier builds were development builds labelled 0.1.0 and
were never published.

## Release identity

| Field | Value |
| --- | --- |
| Version | 0.2.0 (firmware `project()` version, web `package.json` and lockfile agree) |
| Source | Branch `fix/trends-restore-and-csp-validators`; the exact release commit and its CI run are recorded when the release is tagged |
| Release package | Built by CI on `master`: bootloader, partition table, initial OTA selector, application, web build, `SHA256SUMS.txt` and `manifest.json` ([RELEASE_PROVENANCE.md](RELEASE_PROVENANCE.md)) |
| SDK and toolchain | ESP-IDF v6.1 (`fff9895c82d744c7237be8847347bdd1b07c6643`), Xtensa GCC `esp-15.2.0_20251204` |
| Hardware | ESP32-S3-N16R8 (16 MiB flash, 8 MiB PSRAM), 1.8-inch ST7735 LCD, up to five DS18B20 probes on GPIO4 |

## What is new

**Signing in**

- No more owner code. The administrator password is created with the password alone while the
  setup access point is open (first start without saved Wi-Fi, or after a five-second BOOT hold),
  and only once. A refused attempt says why: setup closed, password already set, wait five
  seconds, or the password length.
- Logging in takes about 4 seconds instead of about 30. New passwords use 10,000 PBKDF2 rounds
  with the count stored. A password saved by older firmware still works and is rehashed at its
  next login; that first login takes about 35 seconds.
- The sign-in form says that it is checking the password while it waits.
- Logins with the right password no longer fail at random with "Login denied". If the device
  cannot check a password it now says so (`503`) instead of calling it wrong.
- A stale owner code left in storage by older firmware is erased at startup.

**Web pages**

- Alerts and Events are separate pages. Alerts lists only active conditions (high, low, probe
  disconnected, sensor bus fault, Wi-Fi lost), each with the time it began.
- History draws all selected probes on one chart, lists them in position order, and runs each
  line through missing intervals while still shading the gaps.
- "Live connection lost" appears only after the problem has lasted ten seconds, so brief
  reconnects no longer flash it.
- Settings describes the firmware update state in words.
- The pages work under the device's Content Security Policy: schema validators are compiled at
  build time.

**LCD**

- The Trends graph is restored from stored history after a restart, and short gaps are bridged.
- The trend data lives in PSRAM. Internal RAM, needed by Wi-Fi and the network stack, no longer
  runs out a minute after boot; that shortage had made the device drop web responses at random.
- The display hand-off is cheaper, and dropped display frames are counted in the console's
  `HEALTH` line.

**History and responsiveness**

- History queries read only the storage their time range needs: a one-hour query went from about
  1.4 s to about 0.34 s, so the one-query-per-second limit works.
- History queries and CSV exports run on their own task and send CSV in 4 KiB pieces. A 30-day
  export takes about 3 s instead of 6 s, and other requests keep being answered while it runs.
- A client joining the live feed no longer makes every other client receive a duplicate update.

**Tools** (for development)

- `tools/device_api_test.py` needs `--expect-sensors N` and checks the exact probe count.
- The private files under `.build/` name their passwords `admin_password` and
  `wifi_setup_password`. `device_setup.py` keeps the administrator password when it saves new
  setup details.
- New board tests: `device_soak_profile.py`, `device_history_load_test.py`,
  `device_login_flood_test.py` and `device_wrong_wifi_test.py`.

**API changes** (the web app ships in the same image, so no schema version changed)

- Active alerts carry `since_uptime_ms` and `since_utc_seconds`, and include `wifi_disconnected`.
- `POST /auth/enroll` takes only `password` and answers `201`, or `403`, `409`, `429`, `400` or
  `503` with a reason.
- `POST /auth/login` answers `503` when the device could not check the password.

## Checked on the board

On the connected ESP32-S3 with four saved probes, between 2026-11-10 and 2026-11-12:

- `device_api_test.py --expect-sensors 4` passed all checks (476 on the final firmware).
- Over-the-air updates in both directions, with the state shown in Settings. An update whose web
  server never starts rolled itself back in 129.8 s without a reset.
- A wrong Wi-Fi network kept the saved one; the setup access point closed after 10 minutes, and
  the LCD kept showing readings.
- Repeated wrong logins did not slow the dashboard (slowest reply 0.25 s), and the owner still
  signed in at once.
- A 45-minute load soak: every request answered, no faults, internal RAM flat.
- `./tools/verify.sh --firmware` and CI pass.

The evidence is in [validation/](validation/) and [REVIEW_FIX_TODO.md](REVIEW_FIX_TODO.md).

## Known issues and open checks

| Area | What is open | Tracking |
| --- | --- | --- |
| Long-run stability | Only a 45-minute soak has run. PSRAM use grew about 2.7 KB, matching the trend data filling toward its 24-hour limit; a run longer than 24 hours has not confirmed that it levels off. LVGL memory and flash-write counts are not measured. | R10-10, T10-05 |
| Sensor bus faults | Unplugging the probe bus, and recovery of a probe held above its limit, have not been checked on the board. | R01-07 |
| Other devices | Public access from a phone, and `.local` name resolution from Windows, macOS/iOS and Android, are not checked. Linux works. | R07-08, R08-05 |
| LCD | The update-status footer and the setup page were not watched on the panel (their logs and host renders were checked). The panel's geometry and backlight are not fully measured. | T10-02, T04-02 |
| History retention | The store has not filled yet. Retention under eviction is the host profile's forecast (about 61 days with four probes). | T06-15 |
| Backlight | Do not connect GPIO15 to the LCD backlight until its voltage, polarity and current are measured. | T04-02 |
| Security model | The device uses local HTTP, so traffic is not encrypted on the LAN. Readings, history and display settings are public; changes need the administrator password. Do not expose the device to the internet. | [SECURITY_DESIGN.md](SECURITY_DESIGN.md) |
| History data | History stores five-minute aggregates, not raw five-second readings. Erasing history or reformatting flash destroys it; see [HISTORY_FORMAT.md](HISTORY_FORMAT.md) for backups. | — |

## Upgrading from a 0.1.0 development build

- Install over the air from Settings, or flash without erasing. History, settings, Wi-Fi and the
  administrator password are kept.
- The first login after upgrading takes about 35 seconds while the password is rehashed. Later
  logins take about 4 seconds.
- If a development setup file under `.build/` still uses the old key names, rename `password` to
  `admin_password` in `device-admin.json`. Running `device_setup.py` again rewrites the setup key
  as `wifi_setup_password`.
