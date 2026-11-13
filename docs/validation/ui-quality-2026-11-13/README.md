# UI quality review (review audit A12, gate H11) — 2026-11-13

Simulator and headless-Chromium evidence (DevTools protocol), plus a code review of the pages. No
phone and no board: physical phone acceptance stays with H12 (A20).

## Automated checks (`automated-checks.json`)

Six pages (Dashboard, History, Sensors, Alerts, Events, Settings) × light and dark × 360, 390, 768,
1280 and 1600 px, each page loaded fresh after its first data:

| Check | Result |
| --- | --- |
| Horizontal overflow (scroll width − viewport) | 0 px in all 60 runs |
| Visible controls without an accessible name | none |
| Text below WCAG AA contrast (4.5:1, 3:1 for large text) | none |
| Keyboard: 23–24 Tab stops per page at 1280 px | every stop shows focus (see below) |
| Reduced motion (`prefers-reduced-motion: reduce`) | longest transition 0 ms, no running animation |

Limits of these checks: contrast is computed from computed colours against the nearest opaque
background, so text over images, gradients or translucent layers, and focus/hover states, are not
measured; disabled text is exempt, as WCAG allows. The name check is a heuristic (label, aria,
text, title), not a full accessible-name computation, and no screen reader was used. Tab order was
checked for visible focus, not for logical order on every page.

## Defect found and fixed: switch focus

Keyboard focus on a switch (Sensors "Monitoring enabled", Settings) lands on its transparent input,
so the theme's global `:focus-visible` ring could not show. The only cue was a faint halo around the
thumb, well under 3:1 against the track in the light theme. The track now gets the same 2 px
secondary ring as every other control (`theme.ts`; the light secondary `#4D6356` and dark
`#B4CCBC` both reach at least 3:1 against the surface). Regression: `tokens.test.ts`, "switch
keyboard focus" (both themes). After: `switch-focus-light-after.png`, `switch-focus-dark-after.png`.
Text fields were checked separately: their outline goes from 1 px grey to 2 px primary on focus.

## Loading, empty and error states (code review)

- Loading: skeletons with screen-reader text (`LoadingBlock.tsx`) on every page; Dashboard
  "Loading history…".
- Empty: Alerts and Events show an icon and a sentence; History and Dashboard say no history is
  recorded yet in the range; the Dashboard shows "No active alerts".
- Errors: the message is the device's own reason or a transport message ("Could not reach the
  device", "The device did not answer in time"). Every page that shows one has a retry: Refresh
  on History, Sensors, Alerts and Events, Try again on the Dashboard history notice (A07) and the
  calibration pause (A01), and re-submitting on Settings, Network and sign-in. Read-only sensor
  storage explains itself and now clears on its own (A03).
- No further defect was confirmed.

## Page comparisons

The six-page before/after comparisons at 390 and 1280 px in both themes are in
`../web-redesign-2026-11-12/` (owner-reviewed on 2026-11-12). Views changed since then (Dashboard
history notice, History and Dashboard axes, Sensors read-only text, calibration dialog) were captured
again at 390 and 1280 px in both themes during this run and showed no layout problem. They are not
committed; the overflow figures above cover all five widths.
