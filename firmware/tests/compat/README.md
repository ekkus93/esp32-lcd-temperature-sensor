# Older journal readers (review audit A11, gate H09)

Verbatim copies of `event_store.{hpp,cpp}` from earlier commits, built only into the compatibility
tests. Do not edit them: they stand for firmware already written to boards.

- `journal_tolerant_3c1a01d/`: the last reader before the calibration event kinds existed (commit
  `3c1a01d`, which made unknown kinds readable). A rollback to it must keep working.
- `journal_strict_cefb21b/`: the reader before that (commit `cefb21b`), which rejects unknown kinds.
  The test records that it refuses such a journal: rolling back that far after calibrating is not
  supported.

`journal_old_reader.cpp` drives either one against a journal that `journal_fixture_writer.cpp`
writes with the current code in a fresh temporary directory.
