# Clock synchronization timing after boot — 2026-11-04

Observation: after several USB power cycles the LCD showed `--:--` for minutes even
though Wi-Fi was connected and the board had an IP address. Earlier boots had
synchronized within seconds. The firmware never published a made-up time during the
delay (the clock stays "unsynchronized"), so this was a delay, not a wrong value.

Network checks from the same LAN: the router's DNS resolved `pool.ntp.org` in about
6 ms and four NTP servers answered in 75-81 ms, so the path was healthy.

Measured on the connected board before the change (standard ESP-IDF SNTP started
with the radio, about 1 s after boot): the station got its address at 3.5 s but
the clock was first synchronized between 71 s and 76 s of uptime on one boot, and at
about 265 s on another. The SNTP client is started before the station has an address;
lwIP then retries with a timeout that starts at 15 s and doubles to 150 s, so one
failed first request can delay synchronization by minutes.

Change: `start_clock()` now registers an `IP_EVENT_STA_GOT_IP` handler that calls
`esp_sntp_restart()`, resetting the backoff and sending a query as soon as an
address exists (`firmware/components/clock_service/clock_service.cpp`).

After the change, three consecutive boots (serial HEALTH records every five
seconds): the log line `Network address acquired; SNTP restarted` appeared at the
same millisecond as `sta ip` each time (4.2 s, 2.9 s, 2.4 s); the clock was first
seen synchronized at 36.2 s, 6.3 s and 6.3 s of uptime, with no error lines.

Limits: three boots, one router and network. The 36 s boot shows the first query
after the restart can still be lost, after which lwIP retries after 15 s or more; a
periodic restart while unsynchronized would bound that further but was not added.
Behaviour after a Wi-Fi reconnect (a new address event) uses the same handler and was
not exercised separately. Offline operation is unchanged: the clock stays
unsynchronized and `--:--` is shown.
