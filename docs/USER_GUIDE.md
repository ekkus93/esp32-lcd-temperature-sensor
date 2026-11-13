# Temperature Monitor User Guide

For first-time AP setup, credential retrieval, and reopening the setup window,
see [ONBOARDING.md](ONBOARDING.md).

## Dashboard and live readings

The Dashboard shows each discovered probe by its saved label, current value,
state, ROM address, and age of the last valid reading. It supports up to five
probes. An empty position is not an alarm. A shared-bus fault means readings
from the bus are unavailable; a single disconnected probe is shown separately.
Temperature units are selected in Settings (new devices start in Fahrenheit).
Sensor calibration and alarm limits are entered in the unit you selected and
stored in Celsius.

**Who can see what.** Anyone who can reach the device can open the Dashboard,
History, Alerts and Events without a password. The navigation is a side rail on a wide
screen and a bar along the bottom on a phone. Changing anything needs the
administrator password: the Sensors page and the settings controls show a
sign-in box until you sign in, and **Sign in** / **Sign out** sit at the top of
the page. History is limited to one query per second and one CSV export per 30
seconds for each computer or phone, so a refused request just needs a moment before you retry.

## Identify and configure probes

Open **Sensors** and warm one probe while watching its live value to match it
to a stable ROM address. Set a name and optional short LCD label, enable or
disable monitoring, and choose display order. Calibration adds a fixed offset
(entered in the unit you selected, stored in Celsius). Low/high alarm thresholds can be independently left blank. Hysteresis
sets the return margin, persistence sets how long a condition must remain before
an alert changes, and the disconnect switch controls that probe's disconnect
alerts. Save commits settings before applying them. The editor confirms success
only after the device reports the committed revision. If stored configuration
is unreadable or a save fails, editing is disabled and the prior configuration
is retained for recovery.

At five saved probe identities, a disconnected probe's mapping can be retired
from the Sensors page to make room for a replacement. Retirement requires
confirmation and is saved before the slot is released; disabling monitoring
does not free a slot. If the same ROM later reappears, it is treated as a new
default-configured entry when capacity is available. Existing history remains
associated with that ROM and is never reassigned to another probe.

## Calibrate a probe in ice

Every probe can be calibrated against an ice bath, which needs no other equipment. It corrects
the probe's own error, so readings are right near 0 °C and, because the error barely changes
across a grow room's range, across that range too. Boiling water is not used.

**Make the ice bath.** Fill a tall container with crushed ice, not cubes, and top it up with cold
water until it is just below the ice surface. Stir it and leave it for a minute. Put the probe's
metal tip at least 5 cm into the bath, in the middle, not touching the container. Stir while it
measures, and add ice if it floats free.

**Calibrate each probe on its own cable, as it is installed,** so the offset matches how the probe
will be used. Dry the probe afterwards.

**Run the flow.** Open **Sensors**, choose the probe's tab, and press **Calibrate in ice**.

1. The dialog pauses the probe's alerts while it is in the bath. They stay paused while the
   dialog is open and resume a few minutes after you close it.
2. The dialog waits for a steady reading: the last minute of readings must stay within 0.1 °C.
   It gives up after ten minutes with advice to stir the bath and check that the probe is covered.
3. It shows the offset that makes the probe read 0 °C (32 °F). Save it, or press Cancel to keep the
   current offset. If the probe already reads 0 °C, it says nothing needs saving.
4. After saving, put the probe back in the bath briefly to confirm it reads 0 °C, then dry it.

The dialog refuses an offset beyond ±12.7 °C (about ±22.9 °F), because a probe that far off is
faulty or the bath is wrong. It asks you to check the bath and the probe before saving an offset
beyond 2 °C, which a genuine probe does not need (its error is within ±0.5 °C).

**What calibration changes.** Only the probe's calibration offset is saved. Its name, display
order, enabled state, alarm limits, hysteresis, persistence and disconnect alert stay as they are.
The offset is also written into the probe's own memory, so it stays with the probe.

**Check the limits afterwards.** Alarm limits are compared with the corrected reading. If you set
a limit to allow for a known error, that allowance is now wrong; the dialog offers **Edit limits**
to change them. Calibration never changes a limit itself.

**What the probe's history shows.** While the probe is in the bath its readings are not recorded,
so its history has a gap there. The LCD shows **CAL** in the probe's status place, and the Events
tab records when the pause started and how it ended (time ran out, ended early, or the probe was
lost).

**Mount probes for a grow room.** Calibration corrects the probe; placement decides whether the
number means anything for the plants.

- Shade the probe from the grow lights, for example in a vented white shield or a white tube open at
  both ends. A bare steel probe under light reads air temperature plus heat from the light,
  often 1 to 3 °C high.
- Mount it at canopy height, away from heaters, fan outlets, walls and the hot spot under a lamp.
- Let air move past it gently. A probe in still air reacts slowly and can sit in pockets of heat.
- Use more than one probe where the room varies, and compare canopy level with room level.

Humidity is not measured by these probes. A separate sensor would be needed for it.

## Alerts and event history

The **Alerts** page has two tabs, **Alerts** (the default) and **Events**. The **Alerts** tab shows
only **active alerts**. The **Events** tab lists the journal; the old `/events` address opens it.
A badge on the Alerts icon in the navigation shows how many alerts are active. Active alerts are
conditions that need attention now: a probe reading high or low, a disconnected
probe, a shared sensor bus fault, or a lost Wi-Fi connection. Each one shows when it
began and clears by itself when the condition ends. **Events** are records of things that happened:
device starts, probe and bus state changes, Wi-Fi and clock changes, ice-bath calibration holds
(when a hold started and why it ended), and history write failures. An event may or may not need attention; check the active list for
what is still a problem. The journal is bounded; the page warns when
older records have been compacted or the journal is unavailable. When the clock
is synchronized, event times use the timezone selected in Settings and include
the local zone abbreviation. If that setting is unavailable, synchronized times
are explicitly shown in UTC. Before synchronization, the page shows boot ID and
elapsed uptime instead.

## Temperature history

The **History** page graphs persisted five-minute averages and displays minima,
maxima, valid sample counts, and partial/gap markers. Select 1 hour, 24 hours,
7 days, or 30 days; choose available probe series; use Zoom and Earlier/Later to
inspect a bounded range. Refresh reloads persisted data. CSV export downloads
five-minute aggregate rows, not raw five-second readings. Synchronized history
uses the timezone selected in Settings; unavailable timezone settings fall back
to explicitly labeled UTC. Unknown time is scoped to a device boot and is not
assigned a fabricated date. See
[`HISTORY_FORMAT.md`](HISTORY_FORMAT.md) for CSV fields, retention forecasts,
and backup/recovery procedures.

## Settings, networking, and display preferences

**Settings** configures Celsius/Fahrenheit, timezone, NTP server, and the
read-only display mode/timers. Extended mode defaults to a 20-second Overview
and 10-second Trends dwell; both are independently adjustable. The lookback
controls the LCD trend display. Sensor labels, order, enable state, calibration,
and alarms are edited on the Sensors page.

On the compact 1.8-inch LCD, Trends draws every probe as a line of its own colour on one graph.
The graph has real axes. The temperature axis (left) has round tick values at the top, middle and
bottom, in the unit shown in the title; the scale always covers at least 2 degrees, so a steady room
reads as a nearly flat line, and it contains all of the data. The time axis (bottom) runs from the
lookback (for example `-1h`) through the half-way point to `now`. Under the graph a legend in two
balanced columns names each probe in its line's colour (sky blue, amber, coral, lilac, mint, in
probe order; the first half of the probes on the left, the rest on the right). A probe with no
history yet is still listed in its colour but has no line. With only the Overview and Trends
screens, nothing rotates.

New devices default to the `America/Los_Angeles` timezone, including daylight
saving changes. You can choose another timezone in Settings. Existing saved
timezone choices are preserved when the firmware is updated.

To change Wi-Fi, scan nearby 2.4 GHz networks or enter a hidden SSID and
password, then submit the candidate. The device tests it before replacing saved
credentials; on failure, the old network remains and setup can be retried. The
status area reports connection progress and the LAN address. Clock time remains
unknown until synchronization succeeds. Settings reports the last successful
NTP sync in UTC and its age from the monotonic clock; before the first sync it
states that the time is unknown rather than inferring freshness from wall time.

**The LCD** is a read-only display. Readings are shown in the unit you selected with the other
unit beside them (for example `68.0 °F` and `20.0 °C`), so you never have to convert. In Extended
mode it alternates Overview and Trends; a footer reports Wi-Fi, history logging problems (`Log ERR`)
and a shared sensor-bus fault. While the setup window is open the screen alternates the Wi-Fi setup
page and the normal readings. Holding the BOOT button shows a progress bar.
Whether the panel itself has passed its physical acceptance checks is tracked in
[`LCD_BENCH_CHECKLIST.md`](LCD_BENCH_CHECKLIST.md). The browser is read-only with respect to sensor
measurements; it does not acknowledge or clear active alarms.

**Finding the device on your network.** Besides its IP address, the device answers to
`http://temperature-monitor.local` (the **Network name** in Settings, which you can change; it must
be one lowercase word with letters, digits and hyphens). Settings shows the address in use and the
setup screen offers it once the device is on your Wi-Fi. If another device already uses the name, the
monitor takes the next free one (for example `temperature-monitor-2`) and Settings shows the name it
really got. Some systems cannot resolve `.local` names (Android before version 12, and Linux without
Avahi or nss-mdns); use the IP address there.

## Firmware updates and security

Settings shows the running firmware version, slot, OTA state, and storage
health. **Restart device** asks for confirmation and preserves Wi-Fi,
administrator, sensor, and history data. Factory reset offers two separately
confirmed choices. **Reset settings, preserve history** clears Wi-Fi,
administrator, sensor configuration, and preferences while retaining history.
**Factory reset and erase history** also erases all stored temperature and event
history. Save any needed export first; reset cannot be undone. To update, choose
a compatible ESP-IDF `.bin` image no larger than 4 MiB,
confirm installation, keep the device powered during upload, and wait for the
restart message before reloading the page. The device validates an image before
selecting its boot slot. After the restart Settings shows the new firmware
being checked; if its services do not come up within two minutes it rolls back
to the previous version by itself.

The owner selected local HTTP and accepted its LAN confidentiality limitation.
Authentication and CSRF checks protect actions, but HTTP does not encrypt
passwords, sessions, or Wi-Fi credentials from other devices on the same LAN.
Do not expose the device through WAN port forwarding. Physical USB administrator
recovery is separate and preserves Wi-Fi, sensor settings, and history; see
[`SECURITY_DESIGN.md`](SECURITY_DESIGN.md).
