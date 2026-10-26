# Repository Guidelines

## Project Structure & Module Organization

This repository is currently in the design stage; no application source, build configuration, or tests exist yet.

- `docs/SPEC.md` defines the ESP32-S3 temperature monitor’s system requirements.
- `docs/TODO.md` tracks implementation phases and acceptance checks.
- `docs/LCD_UI_DESIGN_SPEC.md` and `docs/WEB_UI_DESIGN_SPEC.md` describe the interfaces; accompanying SVG/PNG files provide mockups.
- `docs/ESP32_S3_TEMPERATURE_MONITOR_WIRING.md` and its SVG document hardware connections.
- Planned directories are `firmware/` for ESP-IDF C/C++ and `web/` for React 19, TypeScript, and Vite. Keep firmware services modular and sensor/alert logic testable on a host.

## Build, Test, and Development Commands

There are no runnable project build or test commands yet. Once the planned scaffolding exists:

- In an initialized ESP-IDF environment, `idf.py --version` checks SDK identity.
- From `firmware/`, `idf.py set-target esp32s3` selects the chip and `idf.py build` compiles firmware.
- From `firmware/`, `idf.py -p <PORT> flash monitor` flashes a connected board and opens serial logs.

The specification requires ESP-IDF v6.1 with an exact pinned revision/toolchain. Add and document web development, build, lint, typecheck, and test scripts when creating `web/package.json`.

## Coding Style & Naming Conventions

No formatter, linter, or indentation rules are configured. Establish explicit C/C++ and TypeScript formatting rules with the initial scaffolding; follow surrounding style thereafter. Use descriptive service names such as `sensor_registry` and `history_service`. Preserve existing documentation filenames and relative links. Keep specifications, mockups, wiring, and task descriptions consistent when requirements change.

## Testing Guidelines

No testing framework or numerical coverage threshold is established. Add host tests with fake sensors, clocks, and storage, plus frontend unit and API contract tests. Name cases by behavior, covering CRC failures, sensor reconnection, alert hysteresis, and history gaps. Record commands and results; distinguish simulated checks from physical ESP32 verification. Check TODO items only after their acceptance checks pass.

## Commit & Pull Request Guidelines

Git history contains only `Initial commit`, so no commit convention is established. Use concise, imperative subjects describing one focused change. PRs should explain behavior, reference relevant TODO IDs or issues, report validation and hardware limitations, and include screenshots for UI changes.
