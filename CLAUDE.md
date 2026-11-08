# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

@AGENTS.md

## Verification workflow

Run the focused test for the area changed, then `./tools/verify.sh` before declaring done. Add `--firmware` only for firmware/SDK changes. Commit only when asked. Don't edit `docs/TODO.md` or `docs/PROGRESS.md` unless asked; a TODO task may be checked only when its acceptance criteria pass.

`verify.sh` also fails if generated TS types are stale: fix with `npm --prefix web run contract:generate`. It needs `ninja`, `cmake`>=3.22, `rg` and `libpng-dev`. `SKIP_WEB_BUILD=1` skips the web rebuild (CI uses it).

## Focused tests

- Firmware host tests (each is its own ctest entry): `ctest --test-dir .build/host -R storage_recovery --output-on-failure` (configure first: `cmake -S firmware/tests -B .build/host -G Ninja`). Built with `-Wall -Wextra -Werror -Wpedantic`. `history_retention_profile` is opt-in, not a ctest entry.
- Display tests: `ctest --test-dir .build/display -R display_render`. Needs `firmware/managed_components/lvgl__lvgl` (run `idf.py build` first) and `libpng-dev`.
- Web (from `web/`): `npx vitest run src/App.test.tsx` (add `-t "name"`). Simulator: `npm run simulator -- 5`, then `npm run dev` in another terminal.
- Python tools: `python3 -m unittest discover -s tools/tests -p "test_*.py"`.

## Environment

- Before `idf.py` or `verify.sh --firmware`: `source ~/.espressif/tools/activate_idf_v6.1.sh` (ESP-IDF is installed with EIM, not under `~/esp`). `./tools/check-sdk.sh` verifies the SDK.
- Node must be `>=24.13.0 <25` (use `nvm use 24.13.0`, CI's version). Web dependencies are pinned to exact versions. The web `build` script runs `tsc --noEmit` first, and `npm run lint` fails on any warning.
- The firmware embeds the compressed web build: run `npm --prefix web run build` before `idf.py build`.
- clang-format is Espressif's 21.1.3, which `source ~/.espressif/tools/activate_idf_v6.1.sh` puts on PATH; activate IDF before formatting or running `verify.sh`. Other versions (e.g. 18) flag different lines. To fix formatting, run the check pipeline in `tools/verify.sh` (`rg --files -0 firmware -g '*.cpp' -g '*.hpp' -g '*.h'`, excluding build, build-sim, managed_components and generated, piped to `xargs -0 clang-format`) with `-i` instead of `--dry-run --Werror`. The PostToolUse hook in `.claude/settings.json` ends in `|| true`, so it silently does nothing if clang-format is missing.
- Simulation firmware (`CONFIG_MONITOR_SIMULATION=y`) needs its own sdkconfig and build dir (`firmware/build-sim`).

## Gotchas

- Never edit `firmware/managed_components` or generated headers. Dependency versions are pinned in `firmware/main/idf_component.yml` and `dependencies.lock`.
- Don't run `erase-flash` or change the partition table without backing up the history partition (0x820000, 4 MiB). The app must fit both 4 MiB OTA slots.
- Run `python tools/check-secret-leaks.py` before committing. Never print or commit credentials; don't delete the admin file under `.build/`.
- Changes to `contracts/*.schema.json` (and `contracts/API.md`) or storage formats need a deliberate version/migration decision.
- `CONFIG_MONITOR_LCD_ST7735` (default on) selects the 1.8" compact layout; disable it for the original ST7789 design. CI also checks that `firmware/sdkconfig` has `CONFIG_MONITOR_SENSOR_BUS=y` and `CONFIG_ESP_TASK_WDT_PANIC=y`.
- `tools/device_*.py` need a connected board on `/dev/ttyACM0`, a mode-0600 setup file and the activated IDF Python environment. Admin login hashing takes about 15 s on the device, so don't treat that as a hang.
- Keep host, simulated and physical evidence distinct in docs. Builds and simulations are not hardware acceptance.
