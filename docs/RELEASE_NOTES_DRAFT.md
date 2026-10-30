# ESP32-S3 Temperature Monitor — Draft release notes

> **NOT RELEASE APPROVED.** This is a preparatory T11-08 deliverable, not a
> production release, an authorization to flash an owner's device, or
> evidence that hardware, Wi-Fi, OTA, reset or power-loss testing passed.
> The final release version, source SHA, download URL, and owner signoff
> must be assigned only after every release-blocking gate is satisfied.

## Release identity (pending qualification)

| Field | Current status |
| --- | --- |
| Public version | **TBD; no qualified release** |
| Exact release Git SHA and Actions run | **TBD; must match the final built sources** |
| Firmware image, flash plan, checksum manifest | **TBD; final artifacts required** |
| SDK/toolchain | ESP-IDF v6.1 (`fff9895c82d744c7237be8847347bdd1b07c6643`), Xtensa GCC `esp-15.2.0_20251204` |
| Hardware | Target ESP32-S3-N16R8, 16 MiB flash, 8 MiB PSRAM (physical configuration not fully qualified) |
| Approval / evidence links | **TBD; not approved** |

CI development packages are source-commit-bound and declare
`qualification: CI_BUILD_ONLY_NOT_RELEASE_APPROVED`. Their checksums
establish file consistency, not authenticity or production acceptance.
See [RELEASE_PROVENANCE.md](RELEASE_PROVENANCE.md).

## Implemented features pending final device qualification

- One shared RMT-backed 1-Wire bus supports up to five DS18B20 probes,
  stable ROM-keyed sensor settings, temperature offsets and sensor/system
  fault alerts.
- Five-minute aggregated LittleFS history, gap-aware JSON queries,
  authenticated CSV export and bounded event-journal pagination.
- Two normal read-only ST7789/LVGL screens (**Overview** and **Trends**),
  with Basic/Extended mode and configurable dwell periods.
- Locally hosted React 19 web UI for live temperatures, trends, alerts,
  probe settings, Wi-Fi, system health and guarded firmware operations.
- Protected first-run SoftAP, session-authenticated local HTTP/WebSocket,
  candidate station configuration and dual-slot OTA boot-validity policy.

These are implemented capabilities, **not** claims of successful physical
probe, LCD, DHCP, flash-durability or OTA acceptance.

## Verification currently available

The GitHub Actions verifier runs native C++/Python tests, simulated
REST/WebSocket integrations, headless LVGL, React checks and pinned
ESP-IDF v6.1 compilation. CI also creates and uploads a development-only
flash artifact with the bootloader, partition table, initial OTA selector,
application, React assets and a source-bound SHA-256 manifest.
See [PROGRESS.md](PROGRESS.md) for exact-SHA results and
[validation/](validation/) for historical simulated and selected
bare-board evidence. Simulated tests do not close physical hardware gates.

## Known limitations and acceptance blockers

Current TODO count: **123/145 numbered tasks complete**. This draft remains
non-release documentation until all physical and owner signoff gates close.


| Area | Open TODO IDs | Required acceptance evidence |
| --- | --- | --- |
| Board, probes and panel | T02-01/02/03/14/15; T04-01/02/03/14 | Verify board/USB roles, LCD BL electrical characteristics, ST7789 orientation/color/readability/refresh, one 4.7 kΩ pull-up and 1–5 physical probes |
| Wi-Fi and BOOT | T05-10/13/18 | Phone AP onboarding, wrong-password/offline recovery, LCD hold feedback/router-offline behavior, and photographed USB identification. Station DHCP/NTP, prior-credential preservation, captive DNS/OS redirects, and five-second BOOT-to-AP passed on the connected test board. |
| Storage and sockets | T06-14/15/16 | Actual five-minute retention, concurrent export, controlled LittleFS interruption/recovery. T07-10 passed connected-board backpressure and concurrency checks. |
| System actions | — | T09-15 restart, both factory-reset modes and history-retention behavior passed on the user-designated test board. |
| OTA and load | T10-02/05/08/09/10 | Failed-service rollback and LCD update-state visibility, populated history preservation, measured heap/watchdog behavior, 24–72-hour combined sensor/LCD/Wi-Fi/flash soak. T10-01 and T10-03 passed connected-board OTA, A→B→A and early-reset rollback. |
| Final documentation/release | T11-01/08 | Measured wiring revisions, final checksummed release package, accepted limitations and owner approval. T11-09 handoff reconciliation is complete. |

**Physical electrical hold:** Do not connect or drive GPIO15 to the LCD
BL/BLK terminal until the actual backlight voltage, polarity and current
have been measured. Physical display identity and the role of the
second USB-C connector are unconfirmed.

**Network/security limitation:** The documented owner decision uses local
HTTP, so confidentiality on an untrusted LAN is not guaranteed. Never
commit or publish Wi-Fi passwords, admin credentials or session tokens.

**History/data limitation:** Stored samples are *five-minute aggregates*,
not raw five-second readings. When UTC is not trusted the UI/API must
report clock uncertainty rather than fabricate timestamps. Partition
changes, erase-history reset and flash formatting can destroy history;
backup and recovery instructions are in
[HISTORY_FORMAT.md](HISTORY_FORMAT.md).

## Publication checklist (do not bypass)

1. Capture physical and owner-network results for the open gates in
   [TODO.md](TODO.md), with sanitized evidence in
   [PROGRESS.md](PROGRESS.md). Do not check hardware gates from CI alone.
2. Reconcile observed board, USB and LCD wiring against the specification;
   secure explicit owner approval for remaining limitations.
3. Build the final exact release SHA with pinned ESP-IDF v6.1, verify that
   every downloaded image and asset matches `SHA256SUMS.txt`, and match
   the manifest's source commit to the passing Actions run.
4. Assign final version, release notes/known-issues approval, firmware
   artifacts and provenance. **Never relabel a CI-only package as a
   release without physical acceptance.**

## Deferred improvements (outside v1)

The optional roadmap in [TODO.md](TODO.md) remains non-blocking.
Post-implementation code-review findings can be added there as future
improvements; do not extend the present release criteria merely to
accommodate speculative hardening.
