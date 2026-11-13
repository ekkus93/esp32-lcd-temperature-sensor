# Calibration hold lifecycle (review audit A01, finding F1) — 2026-11-12

Host and simulator evidence; physical bath acceptance stays with H10 (A19).

## Defect

`IceCalibrationDialog` cleared the hold renewal together with the measurement timers, so reaching a
steady result, stopping, or giving up ended renewal, and a retry never restarted it. Reproduced by
`holdLifecycle.test.tsx` before the fix: zero renewals over 21 minutes on the result screen, and zero
after a ten-minute give-up and retry.

## Fix

`web/src/features/calibration/holdKeeper.ts` owns the pause for the whole open flow: one renewal
schedule (every five minutes, 15-minute requests), requests sent strictly in order so a slow earlier
request cannot land after the closing tail, the five-minute tail sent once on close (and not at all if
no pause was ever requested), and a failed renewal reported so the dialog stops claiming protection.
The dialog keeps its measurement timers separate, shows "no longer paused" with **Pause alerts
again** when a renewal fails, and does not offer measuring or saving until the pause is confirmed.

## Tests

- `holdKeeper.test.ts` (6): renewal cadence and single tail, one schedule on repeated start, refused
  start, failed renewal and re-establishment, strict ordering, no tail without a start.
- `holdLifecycle.test.tsx` (9): result open past 20 minutes, give-up then retry, failed renewal then
  pause again, a first answer arriving after the page closed, a renewal in flight at Cancel, exactly one
  tail for Cancel, Escape and leaving the page, a failed tail, closing before any request.
- The existing dialog and arithmetic tests pass unchanged (arithmetic, steadiness, faults, ETag,
  unchanged offset, units). The firmware's own 20-minute cap and expiry stay covered by the
  `calibration_hold` host test.

## Simulator, dialog left open on its result (headless Chromium)

| Elapsed | Hold remaining |
| --- | --- |
| 61 s (result shown) | 835 s |
| 271 s | 625 s |
| 301 s (after the five-minute renewal) | 895 s |
| 391 s | 805 s |
| 394 s (after Cancel) | 300 s (the tail) |

The result screen was still showing at 6½ minutes.
