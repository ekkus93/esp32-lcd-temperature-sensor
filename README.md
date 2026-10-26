# ESP32-S3 Temperature Monitor

Local-first temperature monitoring for up to five DS18B20 probes on one bus,
a read-only ST7789 LCD, and a self-hosted React web application.

Implementation is in progress. Firmware now includes bare-board diagnostics,
a configurable 1-Wire adapter, persistent history, a protected setup AP, and
an embedded React app with administrator enrollment and sessions. Core/storage
and session logic have host tests; HTTP/auth has real-device loopback tests.
The web app includes ROM-keyed sensor editing, bounded history graphs, alerts,
network scanning and password changes. A shared LVGL renderer has headless
240×320 render tests; no display GPIO is initialized by the current image.

Wi-Fi association tests need an owner-supplied credentials file. LCD/probe
hardware, full history/settings UI, and OTA/release gates remain open.

- [Requirements](docs/SPEC.md)
- [Implementation checklist](docs/TODO.md)
- [Development and verification](docs/DEVELOPMENT.md)
- [Progress and limitations](docs/PROGRESS.md)
- [Wiring and hardware hold points](docs/ESP32_S3_TEMPERATURE_MONITOR_WIRING.md)

Start with `./tools/verify.sh`. Firmware builds require the pinned ESP-IDF v6.1
environment; see the development guide for setup, flashing and simulation.
