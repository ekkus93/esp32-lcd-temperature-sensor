# Dashboard history bound to its request (review audit A07, finding F6) — 2026-11-13

Browser-test (jsdom) evidence only.

## Defect

The Dashboard stored `{key, value}` but drew `value` whatever the current boot, clock basis and range
were, on the live axis. After a reboot with overlapping uptimes, the previous boot's records were
drawn on the new boot's axis. After the first clock synchronization, uptime records stayed in place
while the UTC answer loaded. Refresh errors were swallowed. Reproduced by the new tests against the
old `Dashboard.tsx`: four of the five fail (reboot, first sync, failed refresh, failure on a new
range). The late-answer test also passes on the old code, which already ignored obsolete answers,
and stays as a guard.

## Fix (`web/src/features/dashboard/Dashboard.tsx`)

- Each request records its context (boot, clock basis, range) and the axis bounds it was made for.
  An answer whose `time_basis` differs from the request is refused, not reinterpreted.
- Only an entry for the current context is drawn. A reboot, the first synchronization or a new range
  shows "Loading history…" until its own answer arrives. Obsolete answers are ignored.
- A failed refresh in the same context keeps the chart on its original bounds, with a warning that
  history could not be refreshed and that live readings are separate, plus **Try again**. A failure
  with nothing compatible shows an error, "No chart to show." and **Try again**.
- Still one history query per range change or five-minute refresh; live updates don't query.
  HistoryView's own key protection is unchanged.

## Tests (`Dashboard.test.tsx`, 5 new)

Reboot with overlapping uptime (new `boot_id` requested, nothing drawn until the new answer); first
synchronization (no `boot_id`, nothing drawn, an uptime answer refused); a late answer for the
previous range ignored (series path unchanged); a failed five-minute refresh keeps the same path
(original bounds) with the notice, then Try again recovers and the path follows the live clock; a
failure on a new range shows the error state. All web tests pass.
