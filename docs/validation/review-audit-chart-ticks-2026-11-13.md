# Chart time labels for any window (review audit A06, finding F5) — 2026-11-13

Unit tests (jsdom, Node's ICU time zones) and a headless-browser look at the simulator. No phone was
used, so this is not physical phone acceptance.

## Defect

`timeTicks()` used four-hour local boundaries for every clock-time window up to two days, so a
one-hour view had no labels whenever it held no multiple of four hours. Reproduced with the new test
against the old code: Los Angeles, `2026-01-01T21:00Z`–`22:00Z` (13:00–14:00 PST) gave `[]`.

## Fix (`web/src/features/chart/scale.ts`, `TrendChart.tsx`)

- Clock-time windows up to two days pick the smallest of 5, 15 or 30 minutes, or 1, 2, 3, 4, 6 or
  12 hours, that gives at most six intervals, on round local times (checked per instant, so
  fractional zones and DST are followed and a skipped hour is never shown). One hour gives quarter
  hours; 24 hours gives four hours (as before); 48 hours gives twelve hours.
- Week and month scales are unchanged (each midnight, every fifth midnight).
- No round time inside the window (a very short zoom): both ends are labelled.
- When clocks go back and a time appears twice, those labels carry the zone abbreviation (PDT/PST).
- Since-boot axes gain 1-, 5- and 10-minute steps for short zooms.
- Invalid or empty windows give no ticks. Windows over 62 days (not drawn by the app) get the two ends
  only, so the work is bounded.
- On a phone, labels are thinned to every other one only when there are more than four, so two end
  labels are both kept.

## Tests (`scale.test.ts`, 17)

The Los Angeles hour (13:00, 13:15, 13:30, 13:45, 14:00); a 10-minute zoom (5-minute steps); a
3-minute zoom off the grid (the ends); 24 h, 48 h, 7 days and 30 days; Kathmandu (+05:45), Kolkata
(+05:30) and Adelaide (+10:30); the Los Angeles fall-back hour (unique labels with PDT and PST); the
spring-forward gap (no 02:xx); invalid windows; a 3-minute uptime axis; and a sweep of 5 zones × 8
spans for finite, ordered, unique, in-window ticks. All 256 web tests pass.

## Browser (headless Chromium, simulator, 360 / 390 / 1280 px)

The simulator never has a synchronized clock, so it shows the since-boot axis; the clock-time axis
is covered by the unit tests above. Visible axis labels:

| Page | 360 px | 390 px | 1280 px |
| --- | --- | --- | --- |
| History | 2 h ago, 1 h ago, Now | 2 h ago, 1 h ago, Now | 2 h ago, 90 min ago, 1 h ago, 30 min ago, Now |
| Dashboard (just after start) | Now | 1 min ago, Now | 1 min ago, Now |

No label overlapped and none was hidden where only two existed.
