---
name: verify-change
description: Run the focused test for the area changed, then the full repo verification, and report results honestly. Use after finishing a change.
disable-model-invocation: true
---

1. Check `git status` / `git diff --stat` to see which areas changed (firmware, firmware/display_tests, web, tools, contracts).
2. Run the focused test for each changed area (see CLAUDE.md "Focused tests"). Fix failures before continuing.
3. Run `./tools/verify.sh`. If any changed file is under `firmware/` (excluding tests) or touches the SDK/sdkconfig, first `source /home/phil/esp/esp-idf-v6.1/export.sh` and run `./tools/verify.sh --firmware` instead.
4. Report outcomes verbatim: failing step, output, and anything skipped. Label results as host or simulated; never claim physical sensor, LCD, network or power-loss acceptance. If GPIO15 backlight or sensor-bus wiring changed, note that the electrical checks in `docs/ESP32_S3_TEMPERATURE_MONITOR_WIRING.md` are still required.
