# God file refactor: baseline (G00.3) — 2026-11-13

Build evidence. Source `1d2ec6c78cc565087ed86e6518a6172df9bdb175` (no firmware change since code commit
`3c52431`, which CI run 43399938296 built). Pinned ESP-IDF v6.1.

## Sizes

| Item | Value |
| --- | --- |
| Application image (local build) | 2,182,064 bytes |
| Allowed growth for this plan (spec §2.5) | 16 KiB, so at most 2,198,448 bytes |
| `http_api.cpp` / `HttpApi::handle()` | 1,428 / 788 lines |
| `main.cpp` / `app_main()` | 538 / 404 lines |

## Tasks (must stay the same, spec §2.6)

| Task | Stack | Priority / core | Where |
| --- | --- | --- | --- |
| HTTP server (`httpd`) | 16,384 | ESP-IDF default, core 1 | `HttpApi::start`, 6 sockets, 12 URI handlers |
| `login` worker | 6,144 | 4, core 1 | `HttpApi::start` |
| `history_http` worker | 8,192 | 2, core 1 | `HttpApi::start` |
| `display_refresh` | 6,144 | as created | `app_main` |
| `trend_restore` (one-shot) | 8,192 | 1 | main loop |
| main task (`app_main`) | 8,192 (`CONFIG_ESP_MAIN_TASK_STACK_SIZE`) | ESP-IDF default | sdkconfig |

## Board figures

Not taken: stack high-water marks, free RAM and API latency need the board flashed and
`tools/device_api_test.py` run, which waits for the owner's confirmation. G19.2 compares against the
current board build if the owner allows a baseline run then (the current firmware can be measured
before the refactored image is flashed).
