#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S firmware/tests -B .build/host -G Ninja
cmake --build .build/host
ctest --test-dir .build/host --output-on-failure
python3 -m unittest discover -s tools/tests -p "test_sensor_hardware_report.py"
npm --prefix web run test:simulator
if [[ -d firmware/managed_components/lvgl__lvgl ]]; then
  cmake -S firmware/display_tests -B .build/display -G Ninja
  cmake --build .build/display -j 6
  ctest --test-dir .build/display --output-on-failure
else
  echo "LVGL render checks unavailable: resolve pinned firmware components with idf.py build first."
  exit 1
fi
rg --files -0 firmware -g '*.cpp' -g '*.hpp' -g '*.h' -g '!**/build/**' -g '!**/managed_components/**' -g '!**/generated/**' | xargs -0 clang-format --dry-run --Werror
npm --prefix web run format:check
npm --prefix web run lint
npm --prefix web run test
npm --prefix web run build
if [[ "${1:-}" == "--firmware" ]]; then
  tools/check-sdk.sh
  (cd firmware && idf.py build)
fi
