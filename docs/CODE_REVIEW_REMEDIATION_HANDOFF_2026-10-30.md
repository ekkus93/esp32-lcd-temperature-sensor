# Code Review Remediation Handoff — 2026-10-30

**Status:** In progress. Software checks pass; substantial remediation and
physical qualification remain open. This is not release approval.

## Current workspace

The remediation checklist is [CODE_REVIEW_REMEDIATION_TODO.md](CODE_REVIEW_REMEDIATION_TODO.md), based on findings in [CODE_REVIEW_TODO_2026-10-30.md](CODE_REVIEW_TODO_2026-10-30.md). The working tree is based on `290640c` and has uncommitted changes. Continue the checklist in dependency order; do not mark hardware tasks complete from simulation.

At handoff, 32 of 61 child checks are complete (36 of 73 checklist rows,
including the completed CR01, CR05, CR07 and CR08 parents). Physical gates remain
unchecked unless their individual item says otherwise; simulated MCU evidence
is identified separately from physical-probe qualification.

Implemented and verified so far: production probe discovery prioritizes managed
ROMs and exposes excess-device status on a separate authenticated endpoint;
history reduction preserves bounded explicit gaps; ordinary JSON bodies have
an overall receive deadline; web timestamps use the configured timezone; auth
retry and neutral dashboard history copy are covered; sensor edits now require
writable storage, commit before publish, and roll back on commit failure.
NVS metadata/blob read failures and unsupported-format preservation have
injected host coverage; all editor fields are disabled when storage is read-only.
Contributor/API/user documentation and affected original TODO statuses have
been reconciled in part. The coverage matrix records remaining page/state gaps.
Authenticated SNTP diagnostics now report the last successful sync epoch and
monotonic age or an explicit unknown state. The normal ESP32 API suite and
post-restart bare-board smoke passed; a later smoke observed the SNTP sync
callback, but the synchronized API payload and network-loss aging still need
device verification. See [clock validation](validation/clock-diagnostics-2026-10-30.txt).

## Verification and device evidence

`source /home/phil/esp/esp-idf-v6.1/export.sh && ./tools/verify.sh --firmware`
passed 23 host CTests, Python and simulator API-contract checks, headless display, 73 web
tests, formatting, lint, typecheck, Vite build, and pinned ESP-IDF v6.1 firmware
build. Sensor settings were also mutated and verified after restart on the
ESP32-S3 using the isolated simulation image. The normal image was restored and
passed a 20-second bare-board smoke with healthy history and five-second health
cadence. See [device sensor-settings results](validation/device-sensor-settings-2026-10-30.txt).
No LCD or physical temperature probes were connected. All five web pages were
also inspected at 360×800 and 1280×900 CSS pixels in Chromium; see [responsive
UI evidence](validation/ui-responsive-2026-10-30.txt). The slow-body HTTP and
post-timeout WebSocket checks passed, but concurrent serial capture exposed
repeated CPU1 idle Task Watchdog warnings during the ~15-second PBKDF2 login.
The warning remains an unresolved software risk; see [slow-body evidence](validation/device-slow-body-2026-10-30.txt).

## Next work and blockers

1. Finish CR03 production API/outcome boundary tests and isolated device-side
   persistence-failure injection; CR04 retirement depends on this.
2. Resolve CPU1 idle Task Watchdog warnings during PBKDF2 and close CR06.5;
   then complete durable sensor mutation outcomes/replacement, display
   scheduling, stale-acquisition presentation, scoped service-boundary work and populated-
   storage profiling. Responsive page-width checks and JSON receipt boundary
   tests are complete.
3. Finish CR11 documentation/status reconciliation and CR12 coverage/review
   evidence.
4. Keep physical probe, LCD, board-identification, onboarding, power-loss,
   populated physical-history, and long-soak gates open until their equipment
   and safe wiring prerequisites are available.

The ESP32-S3 runs the normal firmware and remains available at
`/dev/ttyACM0`. The test-managed admin credential stays in ignored `.build/`;
never copy it to reports or Git. No commit or push was made for this work.
