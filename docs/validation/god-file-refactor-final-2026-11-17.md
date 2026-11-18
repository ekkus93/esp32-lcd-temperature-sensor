# God file refactor: final checks (G19) — 2026-11-17

Host and CI evidence. No board run (G19.2 waits for the owner's confirmation to flash).

## G19.1

- Final commit `7b0bd1d2078def0a0b2db3dbe9d5442d1a9caf29` (the last code change is `ed8d874`).
- Local `./tools/verify.sh --firmware` passed: 61 host tests, 68 Python tests, display tests, 263 web
  tests, formatting, lint, typecheck, `check_unit_size.py --strict` (0 over the limits, 35 allowed)
  and the firmware build.
- CI: run 44235142322 (dispatched by hand on `7b0bd1d`) passed, "Host tests and pinned ESP-IDF build". The
  automatic push runs were superseded or are no longer listed: GitHub's Actions history showed
  nothing after 2026-10-30 when checked on 2026-11-17. The one failure seen during the work (G15,
  `1fdec48`) was the artifact-download step only; its build and tests passed.
- Contracts: `contracts/` and the generated web API types are unchanged since the baseline `1d2ec6c`;
  `verify.sh` reports the generated types match.
- Image: 2,190,976 bytes, 8,912 over the baseline 2,182,064 (allowance 16 KiB).

## Size

| File | Before | After |
| --- | --- | --- |
| `http_api.cpp` | 1,428 lines; `handle()` 788 | 361 lines; `handle()` 13 |
| `main.cpp` | 538 lines; `app_main()` 404 | 67 lines; `app_main()` 55 |

## Open

G19.2: flash, `device_api_test.py`, a 30-minute RAM, stack and latency comparison with the current
build, and the LCD and logging. Task stacks, priorities and cores are unchanged (G00.3 table).
