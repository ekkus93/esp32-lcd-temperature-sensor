# Calibration hold API and older-reader compatibility (review audit A11, gate H09) — 2026-11-13

Host evidence and a firmware build. No board cases: the board has no probes attached, so every
connected-probe case would answer 404 or 409 there and prove nothing new.

## Hold request rules (A11.1)

`http_api/calibration_hold_request.{hpp,cpp}` (new) holds the rules the handler uses:
`probe_can_be_held()` (only normal, high or low; otherwise 409) and `calibration_hold_minutes()`
(exactly `{"minutes": n}`, n a whole number from 1 to 20; otherwise 400). Behaviour is unchanged,
except that very large values are now compared as numbers rather than through cJSON's saturated
integer; both refuse them. `calibration_hold_request` (ctest) covers all nine states; 1, 15, 20 and
20.0 accepted; 0, 21, −1, 1.5, 19.999, ±1e9, a string, a boolean, null, an array, `{}`, a misspelt
key, an extra key, a bare array, number or string, and an unreadable body refused. It builds with
the pinned cJSON, as `history_json_allocation_faults` does.

## Older readers (A11.2)

`firmware/tests/compat/` holds byte-identical copies of `event_store.{hpp,cpp}` from two earlier
commits. `journal_fixture_writer` writes, with the current code, a journal in a fresh temporary
directory: reboot, sensor state, **calibration started**, sensor state, **calibration ended**, Wi-Fi
state, clock sync.

- `journal_older_reader_tolerant_3c1a01d`: the last reader before the calibration kinds existed.
  It recovers the journal, shows the five known records in order, skips the two calibration
  records, pages one at a time without repeating or losing a record, and appends and recovers
  again afterwards. A rollback to it keeps working.
- `journal_older_reader_strict_cefb21b`: the reader before that. It refuses the journal (recovery
  fails, appends are refused) and leaves the file unchanged byte for byte. So a rollback to a
  build before `3c1a01d`, after calibrating, loses the event journal until it is rolled forward
  again or cleared. This is a known limit; such builds were development builds only (0.2.0 is
  the first numbered release).

The opposite direction (the current reader on older data and on unknown kinds) stays covered by
`event_unknown_kind`.

## Retained regressions (A11.3)

The firmware cap and expiry (`calibration_hold`), route access (`route_access`, now with the gate
matrix), event journal and history exclusion, and the CAL label (headless display tests) all pass in
`./tools/verify.sh --firmware` (47 host tests, 59 Python, display, 261 web, firmware build).
