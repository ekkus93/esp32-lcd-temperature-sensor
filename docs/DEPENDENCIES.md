# Dependency baseline

The SDK is ESP-IDF v6.1, revision
`fff9895c82d744c7237be8847347bdd1b07c6643`, with GCC
`esp-15.2.0_20251204`. Firmware versions and registry content hashes are locked
in `firmware/dependencies.lock`; all listed components compiled with that SDK.
Physical LCD/sensor behavior is still unverified.

| Component | Pinned version | Declared license |
| --- | --- | --- |
| ESP-IDF / esp_lcd | v6.1 | Apache-2.0 |
| espressif/onewire_bus | 1.1.2 | Apache-2.0 |
| espressif/ds18b20 | 0.4.0 | Apache-2.0 |
| espressif/esp_lvgl_port | 2.9.0 | Apache-2.0 |
| lvgl/lvgl | 9.6.0~1 | MIT |
| joltwallet/littlefs | 1.23.1 | MIT (port), BSD-3-Clause (LittleFS) |
| espressif/cjson | 1.7.19~2 | MIT |
| React / React DOM | 19.3.1 | MIT |
| MUI | 9.5.0 | MIT |
| Emotion React / Styled | 11.14.0 / 11.14.1 | MIT |
| AJV | 8.20.0 | MIT |
| Vite | 8.3.5 | MIT |

MUI's installed peer metadata explicitly supports React 19. The UI uses system
fonts and no remotely loaded assets. Initial production payload is approximately
179 KB gzip, below the 1 MB compressed target; web assets are now embedded and served by the firmware. Actual combined-image and runtime networking gates remain open.

The npm lockfile declares licenses for every package entry, including platform
optional dependencies and development tools. Its metadata currently includes
MIT, Apache-2.0, ISC, BSD-2/3-Clause, MPL-2.0, Python-2.0, PSF-2.0, MIT-0,
CC-BY-4.0, CC0-1.0 and BlueOak-1.0.0. This is a declared-license inventory, not
a claim that every optional component is bundled. Preserve upstream notices,
identify actual bundled fonts/optional libraries, and package their required
license/attribution files before distribution. LVGL carries additional licenses
under `src/libs/` for optional integrations.
