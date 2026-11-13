# Deterministic Settings save tests (review audit A08, finding F8) — 2026-11-13

Browser-test (jsdom) evidence.

## Defect

`DeviceSettings.test.tsx` checked the last `fetch` call for the settings PATCH and queued the PATCH
answer with `mockResolvedValueOnce`. The page polls `/api/v1/clock` every five seconds, so under
full-suite scheduling the last call could be a clock poll (seen once in the review, and once during
this work's A02 verification: `expected '/api/v1/clock' to be '/api/v1/settings'`).

Reproduced deterministically with the old assertions and fake timers advanced five seconds after
the save: `AssertionError: expected '/api/v1/clock' to be '/api/v1/settings'`.

## Fix

- `routedDevice()` answers by URL and method (clock GET, settings GET, settings PATCH); anything else
  fails the test. Assertions find the PATCH among the calls instead of taking the last one.
- The save test holds the PATCH answer and advances fake time so clock polls land before, during
  and after the save. It checks one PATCH, `If-Match`, the intended values, no response-only fields
  (`revision`, `etag`, `writable`, `schema_version`), and `onApplied` only after the answer.
- The network-name save test uses the same router.
- `afterEach` restores real timers, globals and mocks.

## Checks

- Sending no `If-Match` (a deliberate change to `src/api/settings.ts`) fails the save test.
- Focused file 11/11; full web suite 261/261 on two consecutive runs, with the interleaving forced
  inside the test itself.
