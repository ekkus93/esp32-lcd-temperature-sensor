# Packed web size and OTA slot fit (review audit A20.2, gate H12) — 2026-11-13

Build evidence from the local pinned ESP-IDF v6.1 build of `ae7f41e` (the last firmware change;
A17.1 touched only a host tool). Not a board or phone check; A20.1 (offline device, real phone)
stays open.

| Item | Size |
| --- | --- |
| Web build (`web/dist`, 21 files) | 865,981 bytes |
| Embedded in the firmware (gzip where smaller; `web_assets.json`) | 301,201 bytes |
| of which the Roboto Flex font (stored as is, already compressed) | 67,004 bytes |
| Application image `temperature_monitor.bin` | 2,182,048 bytes |
| Each OTA slot (`ota_0` at 0x20000, `ota_1` at 0x420000) | 4,194,304 bytes |
| Headroom in either slot | 2,012,256 bytes (48.0 %) |

Both slots are the same size, so the image fits either one. The existing release check
(`tools/release_provenance.py`) still computes OTA headroom from the CI build; there is no separate
web size budget in the tree, and none was added. Cached against fresh loads were not measured here:
the device serves hashed assets with a one-year immutable cache and `index.html` with `no-cache` (revalidated on each load),
which A20.1 should observe on a phone.
