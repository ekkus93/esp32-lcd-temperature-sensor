# Web redesign: before and after (R12-07) — 2026-11-12

Host and simulated evidence only. The screenshots come from the host simulator with simulated
probes, not from the board, and so are not hardware acceptance (R12-08 covers the board).

## What is here

- `dashboard-light-phone.png`, `history-dark-desktop.png` and the rest: 24 images, one per page
  (Dashboard, History, Sensors, Alerts, Events, Settings), in light and dark mode, at 390 px phone
  and 1280 px desktop width. Each image puts the state before the Material 3 redesign (left, the
  commit before R12-01) beside the current build (right).
- Method: the pre-redesign build ran from a git worktree of its commit against the same simulator
  binary; both were captured with headless Chromium through the DevTools protocol, one page per
  load, after the first data arrived.

## Layout checks

Horizontal scroll (scroll width minus viewport width) was measured on every page, in both themes, at
360, 390, 768, 1280 and 1600 px. It was 0 px in every case. The pre-redesign build was also 0 px at
390 and 1280 px.

## Quality pass (spec §18.8)

- Loading: the page loading messages show a skeleton of the content's shape, and the words are kept
  for screen readers (`features/LoadingBlock.tsx`). The Dashboard already used skeletons.
- Empty states: Alerts and Events show an icon and a sentence.
- Completed saves appear as a snackbar above the phone's navigation bar; progress and errors stay
  inline.
- Focus: a visible focus ring from the theme. Reduced motion stops the live dot and the transitions
  (`styles.css`).
- Contrast: checked by the token tests (WCAG AA for text and boundaries in both themes).

## Not covered here

- Error messages were not all reviewed for "what to do next"; the ones touched in R12 were.
- The 360 px and 1600 px pages were checked for overflow only, not screenshot by screenshot.
