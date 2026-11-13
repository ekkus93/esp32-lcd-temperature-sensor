# Review audit fixes: starting baseline (A00.1, A00.3) — 2026-11-12

Host evidence only. Starting point for `docs/REVIEW_AUDIT_FIX_TODO.md`.

## Tree and configuration

- Source: `b13be28dbbb24d5fd50b409643876f9ef9c82a54` (the reviewed baseline `b20a4f9` plus the two
  plan documents), clean working tree.
- ESP-IDF v6.1 (pinned, activated with `~/.espressif/tools/activate_idf_v6.1.sh`), Node v24.13.0,
  host tests built with `cmake -S firmware/tests -B .build/host -G Ninja`.

## Focused results before any fix

| Suite | Command | Result |
| --- | --- | --- |
| Firmware host | `ctest --test-dir .build/host` | 41 of 41 passed |
| Python tools | `python3 -m unittest discover -s tools/tests -p "test_*.py"` | 51 passed |
| Web, full suite, run 1 | `npx vitest run` (in `web/`) | 230 of 230 passed |
| Web, full suite, run 2 | same | 230 of 230 passed |

The review saw one full-suite failure in `DeviceSettings.test.tsx` (a `/clock` poll landing as the
last fetch); it did not recur in these two runs. That is the expected nature of the finding (it
depends on scheduling), so A08 reproduces it by forcing the interleaving rather than relying on a
chance failure. Two passing runs are not evidence that it is fixed.

## Host fixtures are isolated (A00.3)

`tools/tests/test_host_fixtures_isolated.py` (part of the Python suite) checks every firmware host and
headless display test source:

- every filesystem path is an `mkdtemp` template under `/tmp`, a file name joined to such a directory,
  or `/dev/full` (an always-failing write target used to inject write errors);
- no test names a serial port, the private setup or backup files under `.build/`, the flashing tool,
  or a network connection. The only sockets are in-process `socketpair(AF_UNIX, ...)` pairs.
  Addresses such as the setup access point's `192.168.4.1` appear only as data.

So the host fault-injection tests cannot reach the device's history, auth or Wi-Fi. Negative cases
in the same file show the check rejects each of those.

## What remains device-only

These cannot be proven on the host and stay with their physical gates:

- The HTTP handler code in `http_api.cpp` and the main loop in `main/main.cpp` compile only for the
  ESP32; host tests reach them through the domain code they call (A00.2 adds seams where a fix needs
  the production boundary).
- Real NVS, LittleFS and flash timing, power loss, the USB console and BOOT button.
- Physical probes, the LCD, Wi-Fi and mDNS clients, phones (gates H01–H12).
