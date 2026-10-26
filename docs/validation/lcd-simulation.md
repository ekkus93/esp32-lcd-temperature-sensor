# Headless LCD verification

These are LVGL 9.6.0 software renders at 240×320. No LCD, backlight or physical
probes were connected. They verify the shared renderer and RAM view models;
panel/controller, DMA transfer, electrical behavior and physical readability
remain open. Firmware compiles this component but does not initialize it or
any display GPIO until a hardware adapter is enabled after electrical review.

Run `cmake -S firmware/display_tests -B .build/display -G Ninja`,
`cmake --build .build/display -j 6`, then
`ctest --test-dir .build/display --output-on-failure`.
The pinned LVGL component must first be resolved by an ESP-IDF build; host PNG
export requires `libpng-dev`. `tools/verify.sh` includes this render gate.

The render test covers zero through five probes, long labels, five simultaneous
warnings, negative Fahrenheit values, missing history intervals, a bounded
24-hour five-minute cache, Basic/Extended monotonic switching, a bus fault,
disabled/lost sensors, BOOT hold feedback, unknown time and simulated setup.
The two normal screens are persistent objects. Plots update only on a changed
history window, identity, range or units; rendering performs no filesystem read.
Names are constrained to one line after visual review caught overlapping wraps.

| Case | Software render |
| --- | --- |
| Empty positions | [Overview](lcd-simulation/overview-empty.png) |
| Five readings | [Overview](lcd-simulation/overview-5.png) |
| Long labels and five warnings | [Overview](lcd-simulation/overview-five-warnings-long-names.png) |
| Negative values and trusted time | [Overview](lcd-simulation/overview-negative-fahrenheit.png) |
| No history | [Trends](lcd-simulation/trends-no-data.png) |
| Missing interval | [Trends](lcd-simulation/trends-negative-with-gap.png) |
| Bus fault | [Overview](lcd-simulation/overview-bus-fault.png) |
| Disabled/lost and BOOT progress | [Overview](lcd-simulation/overview-off-and-lost.png) |
| Temporary setup state | [Provisioning](lcd-simulation/provisioning-simulation-only.png) |

All temperatures and setup text in these images are test fixtures. Saved LCD
settings, physical display integration and timezone/DST application remain open.
