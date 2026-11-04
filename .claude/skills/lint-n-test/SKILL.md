---
name: lint-n-test
description: Lint the files and run all tests. Use when asked to lint and test the repo in one pass.
model: haiku
---

1. Run `./tools/verify.sh` from the repo root. It runs, in order: host C++ tests, Python tool tests, the web simulator test, display tests, the clang-format check, and the web `format:check`, `lint`, `test` and `build` steps.
2. If the display step exits with a missing `firmware/managed_components/lvgl__lvgl` message, report that and stop; don't work around it.
3. Report the result plainly: which steps passed, and for any failure the failing step and its output verbatim. Don't fix anything unless asked.
4. Label results as host or simulated; they are not physical hardware acceptance.
