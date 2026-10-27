# Temperature Monitor User Guide

For first-time AP setup, credential retrieval, and reopening the setup window,
see [ONBOARDING.md](ONBOARDING.md).

## Dashboard and live readings

The Dashboard shows each discovered probe by its saved label, current value,
state, ROM address, and age of the last valid reading. It supports up to five
probes. An empty position is not an alarm. A shared-bus fault means readings
from the bus are unavailable; a single disconnected probe is shown separately.
Temperature units are selected in Settings. Sensor calibration and alarm limits
are always configured in Celsius.

## Identify and configure probes

Open **Sensors** and warm one probe while watching its live value to match it
to a stable ROM address. Set a name and optional short LCD label, enable or
disable monitoring, and choose display order. Calibration adds a fixed Celsius
offset. Low/high alarm thresholds can be independently left blank. Hysteresis
sets the return margin, persistence sets how long a condition must remain before
an alert changes, and the disconnect switch controls that probe's disconnect
alerts. Save applies the queued settings at the next measurement cycle; refresh
after the UI reports the update applied.

## Alerts and event history

The **Alerts** page separates active conditions from recent device events.
Events include probe/bus transitions, Wi-Fi state changes, clock synchronization,
reboots, and history write failures. The journal is bounded; the page warns when
older records have been compacted or the journal is unavailable. When the clock
is synchronized, event times are shown in the browser's local timezone. Before
synchronization, the page shows boot ID and elapsed uptime instead.

## Temperature history

The **History** page graphs persisted five-minute averages and displays minima,
maxima, valid sample counts, and partial/gap markers. Select 1 hour, 24 hours,
7 days, or 30 days; choose available probe series; use Zoom and Earlier/Later to
inspect a bounded range. Refresh reloads persisted data. CSV export downloads
five-minute aggregate rows, not raw five-second readings. Unknown time is scoped
to a device boot and is not assigned a fabricated date. See
[`HISTORY_FORMAT.md`](HISTORY_FORMAT.md) for CSV fields, retention forecasts,
and backup/recovery procedures.

## Settings, networking, and display preferences

**Settings** configures Celsius/Fahrenheit, timezone, NTP server, and the
read-only display mode/timers. Extended mode defaults to a 20-second Overview
and 10-second Trends dwell; both are independently adjustable. The lookback
controls the LCD trend display. Sensor labels, order, enable state, calibration,
and alarms are edited on the Sensors page.

To change Wi-Fi, scan nearby 2.4 GHz networks or enter a hidden SSID and
password, then submit the candidate. The device tests it before replacing saved
credentials; on failure, the old network remains and setup can be retried. The
status area reports connection progress and the LAN address. Clock time remains
unknown until synchronization succeeds.

The physical LCD is currently disabled and has not passed panel/electrical
acceptance. Display preferences can be stored and viewed in Settings, but do not
imply that the LCD is active. The browser is read-only with respect to sensor
measurements; it does not acknowledge or clear active alarms.

## Firmware updates and security

Settings shows the running firmware version, slot, OTA state, and storage
health. **Restart device** asks for confirmation and preserves Wi-Fi,
administrator, sensor, and history data. To update, choose a compatible ESP-IDF `.bin` image no larger than 4 MiB,
confirm installation, keep the device powered during upload, and wait for the
restart message before reloading the page. The device validates an image before
selecting its boot slot. Upload and rollback still require real-device
acceptance; see [TODO.md](TODO.md).

The owner selected local HTTP and accepted its LAN confidentiality limitation.
Authentication and CSRF checks protect actions, but HTTP does not encrypt
passwords, sessions, or Wi-Fi credentials from other devices on the same LAN.
Do not expose the device through WAN port forwarding. Full factory-reset
controls are not yet available. Physical USB administrator
recovery is separate and preserves Wi-Fi, sensor settings, and history; see
[`SECURITY_DESIGN.md`](SECURITY_DESIGN.md).
