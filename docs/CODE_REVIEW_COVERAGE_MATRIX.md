# Remediation UI and State Coverage Matrix

This matrix tracks specification section 10. “Covered” means an automated
regression exists; it does not imply physical qualification. “Partial” means
some required states have coverage. “Open” means the state still needs a
focused check. Hardware-only cases remain separate in the remediation TODO.

| Page / surface | 0 / 1 / 5 probes, long names | Bus failure, disconnected / disabled | Empty history, unknown time | Lost WebSocket / unavailable data | Storage failure |
| --- | --- | --- | --- | --- | --- |
| Dashboard | Covered — `App.test.tsx` covers 0 probes, 1 probe, 5 probes and long labels. | Partial — mixed bus, disconnected, disabled and healthy sensors covered in `App.test.tsx`; shared-bus fault recurrence needs device evidence. | Covered — neutral history navigation is independent of availability; no false empty claim. | Partial — `App.test.tsx` covers unavailable live data; WebSocket reconnect UX needs a browser/device check. | Partial — unavailable storage is visible in Settings diagnostics; live dashboard storage-failure propagation needs browser integration evidence. |
| History | Partial — retained historical series and gap overlays are tested; explicit 0/1/5 selectors and long labels need checks. | Open — disconnected/disabled intervals need populated-history integration fixtures. | Covered — empty and boot-relative states in `HistoryView.test.tsx`; DST/device-zone format helper tests. | N/A — History queries HTTP independently and does not consume the live WebSocket. | Partial — unavailable history error state exists; storage corruption fixture needs integration coverage. |
| Sensors | Covered — `SensorsView.test.tsx` covers 0/1/5 managed sensors and 32-character names. | Partial — disabled/disconnected settings are represented; bus fault belongs to Dashboard live status, not this settings editor. | N/A — page edits live settings and does not display history timestamps. | N/A — this editor fetches the sensor-settings endpoint directly and does not subscribe to live WebSocket updates; session recovery remains covered by `AuthGate.test.tsx`. | Partial — read-only settings behavior is covered; sensor-store failure through production persistence remains open. |
| Alerts | Partial — active sensor/bus and event pagination covered in `AlertsView.test.tsx`; probe counts/names are not central to event rendering. | Covered — bus and probe transition display tested. | Covered — uptime events and configured-zone synchronized formatting covered; empty events and unknown-time boot-relative formatting are tested. | Partial — alerts poll independently; API errors are rendered, but reconnect/page recovery race needs a browser check. | Partial — unhealthy event-log warning is implemented; durable failure injection through the production service remains open. |
| Settings | N/A — probe count does not change device preference editing. | N/A — sensor runtime states are shown on Dashboard/Sensors. | N/A — timezone preference is edited here; timestamps are rendered on History/Alerts. | Partial — request errors and read-only preference status are covered; live preference refresh after reconnect needs a browser check. | Partial — device-settings read-only mode is tested; corrupt NVS sensor-store behavior needs a separate integration check. |
| Setup / Auth | N/A — probe topology does not affect enrollment/login. | N/A — sensor runtime states do not affect authentication. | N/A — no timestamps or history. | Partial — initial failure, repeated retry, session expiry and unmounted responses covered in `AuthGate.test.tsx`; real network interruption remains open. | N/A — this UI does not expose sensor/history storage controls. |

## Cross-page checks still required

- Dashboard, History, Sensors, Alerts and Settings were checked at 360×800 and
  1280×900 CSS px for overflow, keyboard access and status/error readability;
  see [responsive UI evidence](validation/ui-responsive-2026-10-30.txt).
- Dashboard and Sensors cover zero/one/five probe counts and maximum labels;
  shared-bus loss is represented in fixtures, while real acquisition staleness
  and fault recurrence still need device injection.
- Verify populated/empty/unavailable history for each supported range, device
  timezone changes, and WebSocket loss/recovery in a browser session.
- Inject isolated settings/history/event storage failures through production
  adapters. Host policy tests and simulation do not substitute for these checks.
- Keep LCD, physical probes, protected-network onboarding, power interruption,
  populated-history OTA, and long soak checks under their physical TODO gates.
