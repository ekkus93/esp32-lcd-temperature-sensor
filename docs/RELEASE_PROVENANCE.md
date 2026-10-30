# CI-only build provenance and release qualification

The exact-SHA GitHub Actions verifier packages **development-only** ESP32-S3
flash images and the matching built React assets. The provenance stage is
implemented in `tools/release_provenance.py`; it runs after host, display,
web and pinned ESP-IDF v6.1 checks have passed. This is a software artifact
integrity check, **not** authorization to flash an owner device or publish
a release.

## Artifact contents and checks

The artifact is named `esp32-s3-firmware-<40-character-source-SHA>`. It
contains the ESP-IDF bootloader, partition table, application image and
`flasher_args.json`; the compiled `web/dist/` files; `web/package.json`,
`web/package-lock.json` and `firmware/partitions.csv`; and
`firmware/build/provenance/manifest.json` plus `SHA256SUMS.txt`.

The generator verifies the exact source SHA syntax; required bootloader,
partition-table, OTA-selector and application flash offsets; no flash-image overlap;
the complete 16 MiB partition map (required entries, types, alignment,
non-overlap and flash bounds), with the bounded ESP-IDF initial OTA selector
image allowed only at the `otadata` partition start;
The generator also decodes the *actual flashed* ESP-IDF partition-table
binary, validates its MD5 marker/digest and padding, and compares every
partition's type/subtype, offset and size against the source CSV. It rejects
stale binary/CSV mismatches even if the binary has a recalculated valid MD5,
as well as invalid flags and duplicate labels. Corrected exact-source
`656fdc0f2c887326b5f468eddd022410e761572f` passed Actions
`40990485662` (native, web, headless display, pinned ESP-IDF v6.1 and
provenance artifact generation).
both 4 MiB OTA slots and application headroom; firmware, web and lockfile
version agreement; nonempty regular files without symlink/traversal paths;
and SHA-256 of all flash images, web assets and pinned inputs. The manifest
also records `flash_size_bytes` and `partition_layout` for independent review.
The flash plan is additionally restricted to the four pinned image names;
extra images in otherwise unused flash gaps are rejected. CSV partition
flags must be empty, and rows must have exactly six fields, preventing
unreviewed encrypted/read-only settings from being silently ignored.
These checks passed exact-SHA Actions `40991655488` at source
`9f269a6811f5c9e6780107c35d6ba6ab5198af59`.
The full-layout and OTA-selector checks passed exact-SHA Actions
`40985438288` at source `d89b505c0eb5b47e4e193fb097ff368ee92367ed`.
It records ESP-IDF commit `fff9895c82d744c7237be8847347bdd1b07c6643`
and explicitly declares
`"qualification": "CI_BUILD_ONLY_NOT_RELEASE_APPROVED"`.
A SHA-256 digest proves file consistency with the manifest; it is **not**
a signature, hardware test, or independent publisher authenticity proof.

The CI upload list now explicitly includes
`firmware/build/ota_data_initial.bin`; previously the manifest and
checksum file referenced it but the artifact omitted it. A Python
regression compares the checksum inputs with the upload path list so a
future missing flash image fails CI. Corrected exact source
`b9e6b763bfc1dce6c9063dcad7bf79b11cf8e9e9` passed Actions
`40993467640` with artifact upload ID `12950726328`. The archive was
not independently downloaded/extracted as part of this check.

To check a downloaded artifact, extract it with the original directory
structure intact, then run from the extraction root:

```sh
sha256sum -c firmware/build/provenance/SHA256SUMS.txt
python3 -m json.tool firmware/build/provenance/manifest.json
```

Compare the manifest's `source_commit` to the exact SHA of the successful
Actions run, inspect `qualification`, and retain the corresponding CI
run ID and artifact ID in the release evidence. The archive may expire
under repository retention policy; do not assume the configured 14-day
upload retention is guaranteed by the hosting policy.

## Release gate (still blocked)

Do **not** rename this artifact a production release or claim OTA, power-loss,
sensor, LCD, or Wi-Fi acceptance. The preparatory [draft release notes](RELEASE_NOTES_DRAFT.md) are not an
approved release. Before completing
[T11-08](TODO.md), the owner must provide physical acceptance evidence for
the board/pins/backlight, five probes and 1-Wire pull-up, live display and
Wi-Fi provisioning, LittleFS power interruption, successful OTA slot
transitions/rollback, isolated factory-reset behavior and 24–72-hour
sustained operation. Final release notes and known-issue signoff must
identify any accepted limitations. See [TODO.md](TODO.md) and
[PROGRESS.md](PROGRESS.md). Never use this checklist to flash owner devices
or operate destructive tests automatically.
