# Current documentation reconciled (review audit A09, finding F9) — 2026-11-13

Documentation review against the code and the recorded results; no new device evidence.

## Corrections

- **CSV access** (A09.1): `DEVELOPMENT.md` and `HISTORY_FORMAT.md` said export needs an administrator
  session. `route_access.cpp` lists `/history/export` as public and the public limiter allows one
  export per 30 seconds per client, so both now say public and rate limited.
- **Completion, deadlines, performance** (A09.2): `HISTORY_FORMAT.md` and `DEVELOPMENT.md` describe
  the A02/A04 behaviour (503 before the first row, closed without the final chunk after it, a
  partial file is not an export; 3 s / 15 s budgets from holding the lock, stop within one 4 KiB
  chunk or record, blocking primitives not pre-empted). The 2026-11-12 broad-query figures (1.43 to
  1.93 s) are labelled as dated board measurements that miss the one-second objective; H07 stays
  open. The A03 compatibility decision (schema v1 kept, timed `read_only`) is in `contracts/API.md`.
  `HISTORY_FORMAT.md`'s backup command now activates IDF through EIM, not `~/esp`.
- **Live socket** (A09.4): `SPEC.md`'s route table called it "Authenticated"; it is public with an
  allowed Origin and Host and four clients, one kept for the signed-in owner (as its security
  section, `SECURITY_DESIGN.md` and `ws_live` already said).
- **Fonts** (A09.4): `FONTS.md` matches the shipped `roboto-flex-latin.woff2` (67,004 bytes, SHA-256
  `49bf5771…5d8c`) and the `font/woff2` content type in `tools/pack-web.py`. No change.
- **Owner code** (A09.4): remaining mentions are dated trackers, the 0.2.0 release notes describing
  its removal, and `SECURITY_DESIGN.md`'s note that a stale one is erased. No change.

## Recount (A09.3)

Counted from `**Tnn-nn**` checkbox IDs in `docs/TODO.md`: 145 unique numbered tasks, 128 complete,
17 open (T02-01/02/03/15, T04-02/14, T05-13/18, T06-15/16, T10-02/05/08/09/10, T11-01/08).
Acceptance gates are not numbered tasks.

- Corrected: the summary paragraph in `docs/TODO.md` (said 125/20 with a stale open list), and the
  top of `docs/HANDOFF.md` (now a dated 2026-11-13 status; the 2026-10-31 and 2026-10-30 sections
  keep their counts as history, without "Current" in their headings).
- Already right: T11-09 in `docs/TODO.md` and `docs/REVIEW_FIX_TODO.md` (128/145, 17 open).
- Left as dated history: `docs/PROGRESS.md` (its top entry is the 2026-10-31 record; CLAUDE.md asks
  for it not to be edited unless the owner asks) and the dated handoff files.

## Original tasks annotated (A09.5)

`docs/REVIEW_FIX_TODO.md` gains a "Review audit 2026-11-13" note under R02-05, R02-09, R04-07,
R04-08, R05-03, R06-01, R06-08, R07-04, R09-08, R10-03, R10-11, R10-13, R11-01, R11-02, R11-04,
R12-04, R12-05 and R12-06, naming the fix task and commit. No box changed: R11-01 and R11-04 stay open
for the physical bath (H10), and R10-03's latency objective stays open under H07.
