# Repository Guidelines

## Project Structure

This ESP32-S3 temperature monitor uses ESP-IDF v6.1 in `firmware/`, with services under `firmware/components/` and startup in `firmware/main/`. Host C++ tests live in `firmware/tests/`; headless LVGL tests are in `firmware/display_tests/`. The React 19, TypeScript and Vite client is in `web/src/`; shared API schemas are in `contracts/`. Requirements, UI/wiring designs and hardware validation records live in `docs/`.

## Build and Verification

Activate the pinned ESP-IDF v6.1 environment as described in `docs/DEVELOPMENT.md`. Run `./tools/verify.sh` for host, simulator, display, web, formatting and lint checks. Run `./tools/verify.sh --firmware` for those checks plus the pinned SDK check and firmware build. From `web/`, `npm ci` installs locked dependencies; `npm run dev` starts Vite, and `npm test`, `npm run lint`, `npm run format:check`, and `npm run build` run focused checks. Device scripts under `tools/device_*.py` require a connected board and setup file; follow `docs/DEVELOPMENT.md` and protect credentials.

## Coding and Testing

Follow existing C++ formatting (`clang-format`) and TypeScript formatting (Prettier); ESLint treats warnings as errors. Keep firmware services modular and sensor, history and protocol logic host-testable. Name tests for behavior and failure conditions. Run focused tests during development and the full verification script before integration. Distinguish host simulations from physical sensor, LCD, network and power-loss acceptance.

## Changes and Hardware

Use concise imperative commit subjects. Describe behavior, relevant TODO IDs, verification commands and hardware limitations in reviews. Handle API/schema and persistent-storage compatibility deliberately. GPIO15 backlight and sensor-bus wiring require the electrical checks in `docs/ESP32_S3_TEMPERATURE_MONITOR_WIRING.md`; builds and simulations do not establish physical acceptance. Keep secrets and device setup files out of version control.
