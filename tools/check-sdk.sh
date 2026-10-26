#!/usr/bin/env bash
set -euo pipefail
: "${IDF_PATH:?Source ESP-IDF v6.1 export.sh first}"
expected=fff9895c82d744c7237be8847347bdd1b07c6643
actual=$(git -C "$IDF_PATH" rev-parse HEAD)
if [[ "$actual" != "$expected" ]]; then
  echo "Expected ESP-IDF v6.1 revision $expected; got $actual" >&2
  exit 1
fi
idf.py --version
xtensa-esp-elf-gcc --version | head -1
