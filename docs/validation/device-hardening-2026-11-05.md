# On-board checks of the review fixes — 2026-11-05

Board: the connected ESP32-S3 with the ST7735 LCD, one probe plugged in (four known
sensors), Wi-Fi up. Firmware: the working tree at commits through R02-06 plus the
client-identity fix below, built with ESP-IDF v6.1 and flashed over USB with a plain
`idf.py flash` (no erase). Tests ran from a Linux machine on the same LAN using the
test-managed administrator file (credentials not printed).

## Boot

Boot log: sensor bus ready, clock synchronized at about 6 s, no error, panic or watchdog
line, no reset after the first. (R01-06 bus on by default, R02-02 watchdog panic enabled.)

## Login hardening (R02-03, part of R02-08)

First run, one client address:
- Login returns 200 through the new async worker (about 30 s for the password check).
- A second login while one is running is refused with `429` and `Retry-After: 1` in 0.13-0.26 s.
- While a wrong-password check ran for 31 s, 16-17 status polls all returned 200 (worst 0.32-0.43 s)
  and a live WebSocket stayed open and delivered frames: the HTTP server is no longer blocked.
- **Defect found:** the retry immediately after a wrong password was *not* locked out. The client
  identifier hashed the whole IPv6 socket address, including the source port, which changes with every
  connection, so the throttle never recognised the same client. Fixed by identifying a client by
  address only (`client_identity_ipv6`; an IPv4-mapped address equals the plain IPv4 address),
  with host tests.
- After the fix: wrong password 401, then an immediate retry gets `429` with `Retry-After: 5` in
  0.05 s, and a login after the lockout succeeds.

Not tested: the owner signing in from a second machine while another address is locked out
(needs a second device); the host test covers this per-client rule.

## Watchdog stability (R02-02)

With the task-watchdog panic enabled, three rounds of 120 s authenticated load (three HTTP workers
cycling status, sensors, alerts, bounded history and CSV export, plus four WebSocket clients)
ran with the same boot id throughout and uptime continuous from 337 s to 712 s: no reset. The
rollback timeout itself is still untested on hardware (R02-07).

## History query latency (R02-09, not closed)

On an idle board with only 188 KB of history stored, `GET /history?...&max_points=512` took 10.3 s and
`GET /history/export` 9.5 s, returning 297 and 1,376 bytes. Under the load above the median request
took about 9 s and the slowest about 27 s, because these queries run on the single HTTP task. The cost
does not depend on the amount of data, so it is mostly fixed per-query overhead; this is the problem R02-05
addresses and it must be fixed before history becomes public.

## History query fix (R02-05) — measured on the board

Cause: `HistoryStore::visit` did a directory and a segment filesystem lookup after every record and
read each segment about four times. Fix: read each 32 KiB segment once into memory, validate it and
deliver records from that snapshot; add a 3 s query budget that marks the answer truncated.

Same board and test machine, idle board (about 188 KB of history):

| Request | Before | After |
| --- | --- | --- |
| `GET /history?...&max_points=512` | 10.3 s | 0.34 s, then 0.61 s |
| `GET /history/export` | 9.5 s | 0.64 s |

Heavy authenticated load (three HTTP workers over status, sensors, alerts, history and CSV export plus four
WebSocket clients, 120 s): before 35 requests, p50 9.4 s, p95 27.5 s; after 625 requests, all 200, p50
0.49 s, p95 0.88 s, max 5.4 s; same boot id and no reset. One of the four WebSocket clients was closed
by the server in each run (three still open), consistent with the four-slot client limit; not investigated here.
Not measured: CSV export at full retention (about 3 MiB of history, weeks of data).
