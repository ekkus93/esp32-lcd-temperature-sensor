# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

@AGENTS.md

## Verification workflow

Run the focused test for the area changed, then `./tools/verify.sh` before declaring done. Add `--firmware` only for firmware/SDK changes. Commit only when asked. Don't edit `docs/TODO.md` or `docs/PROGRESS.md` unless asked; a TODO task may be checked only when its acceptance criteria pass.

## Focused tests

- Firmware host tests (each is its own ctest entry): `ctest --test-dir .build/host -R storage_recovery --output-on-failure` (configure first: `cmake -S firmware/tests -B .build/host -G Ninja`). Built with `-Wall -Wextra -Werror -Wpedantic`. `history_retention_profile` is opt-in, not a ctest entry.
- Display tests: `ctest --test-dir .build/display -R display_render`. Needs `firmware/managed_components/lvgl__lvgl` (run `idf.py build` first) and `libpng-dev`.
- Web (from `web/`): `npx vitest run src/App.test.tsx` (add `-t "name"`). Simulator: `npm run simulator -- 5`, then `npm run dev` in another terminal.
- Python tools: `python3 -m unittest discover -s tools/tests -p "test_*.py"`.

## Environment

- Before `idf.py` or `verify.sh --firmware`: `source /home/phil/esp/esp-idf-v6.1/export.sh`. `./tools/check-sdk.sh` verifies the SDK.
- Node must be `>=24.13.0 <25`. Web dependencies are pinned to exact versions.
- The firmware embeds the compressed web build: run `npm --prefix web run build` before `idf.py build`.
- clang-format must be version 18. Fix C++ formatting with the check command in `tools/verify.sh`, using `-i`.
- Simulation firmware (`CONFIG_MONITOR_SIMULATION=y`) needs its own sdkconfig and build dir (`firmware/build-sim`).

## Gotchas

- Never edit `firmware/managed_components` or generated headers. Dependency versions are pinned in `firmware/main/idf_component.yml` and `dependencies.lock`.
- Don't run `erase-flash` or change the partition table without backing up the history partition (0x820000, 4 MiB). The app must fit both 4 MiB OTA slots.
- Run `python tools/check-secret-leaks.py` before committing. Never print or commit credentials; don't delete the admin file under `.build/`.
- Changes to `contracts/*.schema.json` (and `contracts/API.md`) or storage formats need a deliberate version/migration decision.
- Keep host, simulated and physical evidence distinct in docs. Builds and simulations are not hardware acceptance.
