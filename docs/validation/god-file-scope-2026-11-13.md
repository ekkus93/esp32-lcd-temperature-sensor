# God file refactor: scope (G00.2) — 2026-11-13

`python3 tools/check_unit_size.py` on `2d63bc0` found 36 items over the limits (files over 400 lines,
functions over 80). Targets of this plan, not on the allow-list:

- `firmware/components/http_api/http_api.cpp`: 1,428 lines; `HttpApi::handle()` 788 lines.
- `firmware/main/main.cpp`: 538 lines; `app_main()` 404 lines.

The other 32 are recorded in `tools/unit_size_allowlist.txt`, each with its reason:

| Decision | Items |
| --- | --- |
| Keep: cohesive, length is layout or configuration | `display_view.cpp` (widget tree), `theme.ts` (theme object), page and card components that are mostly JSX (`EventsView`, `NetworkSettings`, `SystemControls`, `HistoryView`, `Dashboard`), `live.ts` (one reconnect closure), `scale.ts` (reworked and tested in A06), `ota_service.cpp` and `PasswordSettings` (one or two lines over) |
| Keep: integrity-critical storage, recently reviewed | `event_store.cpp` (`recover`, `append`) |
| Keep for now, follow-up candidates outside this plan | `history.cpp` (`HistoryStore::append` 238 lines), `wifi_service.cpp` (start and worker), `usb_bridge.cpp` (four similar bridge loops), `display_runtime.cpp` (`initialize`), `SensorsView.tsx` (list and editor in one file), `IceCalibrationDialog.tsx` (405-line component) |

The follow-up candidates are not God files in the sense of this plan: each has one
responsibility, but one or two long functions. They are listed so they are not forgotten. G18 makes
the check strict, so any new long file or function, or any allow-list entry that is no longer
needed, fails `tools/verify.sh`.

## Addendum: multi-line signatures (G13)

The first version of the tool found only functions whose `{` is on the signature's first line. Fixed
during G13, it also finds `AlertService::update()` (86 lines), `DisplayView::render()` (205) and
`HistoryStore::visit()` (140), and `HttpApi::start()` (86), which is in scope and split in G13. The
three others are kept and added to the allow-list: one alert state-machine step, one LCD frame
(a follow-up candidate), and the integrity-critical history visit.
