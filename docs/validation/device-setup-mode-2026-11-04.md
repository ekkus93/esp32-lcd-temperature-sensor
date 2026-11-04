# BOOT-hold setup mode and shorter setup credentials — 2026-11-04

## Setup mode while monitoring (T10-10 partial, T05-10 not closed)

The owner held BOOT on the connected board (ST7735 LCD, one DS18B20 probe, Wi-Fi up)
while the API was polled every two seconds without opening the USB serial port
(opening it resets this board, and a reset with BOOT held enters ROM download
mode). The authenticated session took 32 seconds to establish, so the hold start
and the on-screen hold progress were not captured by the poll. Readings from
31.7 s to 110.7 s after the poll began: Wi-Fi provisioning state `true` and station
state `connected` throughout; uptime advanced continuously from 757.2 s to
837.0 s (no reset); the probe stayed `normal` at 19.25-19.38 °C with `last_seen`
advancing every five seconds; logging stayed healthy. The owner reported seeing
the SoftAP setup screens and gave formatting feedback on them (below); they did not
report on the hold-progress indicator. Not tested: setup while the router is
unavailable, the hold progress display, and automatic exit from setup mode.

## Owner feedback and changes

Feedback: format the SoftAP screens better, and use a reasonable-length password
rather than a 32-character one. The setup Wi-Fi key was 32 lowercase hex
characters and the owner setup code was 64.

Changes: both are now random codes from a 32-character alphabet without `0`, `1`,
`I`, `O` (5 bits per character, no modulo bias): a 12-character Wi-Fi key (60
bits; WPA2 needs 8-63) and a 16-character owner code (80 bits, one time only).
Existing devices keep working: a stored 32-hex Wi-Fi key or 64-hex owner code is
replaced once at boot with the new format, and if that write fails the old value
is kept. On the connected board the first boot logged `Setup Wi-Fi key replaced
with a shorter format`, a second boot logged no rotation and no errors, and sampling
and Wi-Fi were normal. The board was already enrolled, so no owner code was
shown or rotated there. The setup screens were rebuilt with numbered steps,
muted headings and bright values; the compact screen shows `TempMonitor-` and the
suffix on two lines because the full SSID is wider than its 128 px width, and
alternates a Wi-Fi page and an owner-code page only when an owner code exists.
Screenshots are in `docs/validation/lcd-simulation/`. The physical appearance of
the new screens on the LCD and typing the key on a phone have not been checked.

## Physical appearance of the new compact setup screen

After a second BOOT hold of five seconds on the connected board (setup mode open,
uptime, probe readings and logging continuing without a reset), the owner sent a
photo of the real 128 × 160 ST7735 LCD. It shows the new layout complete and
unclipped: "Wi-Fi setup", "1 Join this Wi-Fi", the SSID `TempMonitor-` and its
six-digit suffix on two lines, "Password" with the 12-character key on one line,
"2 Open in a browser" and `192.168.4.1`, and no page marker (the device is
enrolled, so there is no owner code page). The photo has a strong blue cast, so
the muted-heading and bright-value colours could not be judged from it. The
photo is not stored in the repository because it shows a live setup password.
The owner then confirmed seeing the hold-progress percentage on the LCD during
the hold and liked it; that closes the LCD progress, SoftAP-entry and
continued-sampling criteria of T05-10 on the connected board. Not confirmed: the
owner-code page (no unenrolled device was available), typing the key on a phone,
and setup while the router is unavailable (T10-10). After the photo the board was
reset over USB and returned to normal operation with settings preserved.
