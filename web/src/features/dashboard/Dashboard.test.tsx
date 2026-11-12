import { act, fireEvent, screen, within } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import Dashboard from './Dashboard';
import { parseSnapshot } from '../../api/client';
import { empty } from '../../api/fixtures';
import { renderThemed as render } from '../../renderThemed';
import { tokens } from '../../theme/tokens.generated';
import type { ActiveAlert } from '../alertText';

const config = (
  rom: string,
  name: string,
  order: number,
  low: number,
  high: number,
) => ({
  rom_id: rom,
  etag: `"x:${rom}:1"`,
  revision: 1,
  name,
  short_name: '',
  enabled: true,
  disconnect_alert: true,
  order,
  calibration_c: 0,
  low_c: low,
  high_c: high,
  hysteresis_c: 0.5,
  persistence_seconds: 15,
  state: 'normal',
  probe_calibration: 'stored',
});
const CANOPY = '2801000000000029';
const EXHAUST = '280200000000002a';
const snapshot = parseSnapshot({
  ...empty,
  simulated: false,
  uptime_ms: 3_600_000,
  sensors: [
    {
      rom_id: CANOPY,
      name: 'Canopy',
      state: 'normal',
      temperature_c: 24.6,
      last_seen_ms: 3_597_000,
      enabled: true,
      order: 0,
    },
    {
      rom_id: EXHAUST,
      name: 'Exhaust',
      state: 'high',
      temperature_c: 29.4,
      last_seen_ms: 3_597_000,
      enabled: true,
      order: 1,
    },
  ],
});
const point = (rom: string, start: number, average: number) => ({
  boot_id: '0123456789abcdef',
  rom_id: rom,
  start_ms: start,
  end_ms: start + 300_000,
  utc_seconds: null,
  average_c: average,
  minimum_c: average - 0.2,
  maximum_c: average + 0.2,
  count: 60,
  partial: false,
});
const history = {
  schema_version: 1,
  simulated: false,
  truncated: false,
  time_basis: 'uptime',
  points: [0, 1, 2, 3].flatMap((i) => [
    point(CANOPY, 3_000_000 + i * 300_000, 24 + i * 0.1),
    point(EXHAUST, 3_000_000 + i * 300_000, 28 + i * 0.4),
  ]),
  gaps: [],
};
const warm: ActiveAlert = {
  rom_id: EXHAUST,
  name: 'Exhaust',
  state: 'high',
  since_uptime_ms: 3_600_000 - 12 * 60_000,
  since_utc_seconds: null,
};

beforeEach(() => {
  vi.stubGlobal(
    'fetch',
    vi.fn(async (input: RequestInfo | URL) => ({
      ok: true,
      json: async () =>
        String(input).includes('/history')
          ? history
          : [
              config(CANOPY, 'Canopy', 0, 18, 28),
              config(EXHAUST, 'Exhaust', 1, 15, 28),
            ],
    })),
  );
});
afterEach(() => vi.unstubAllGlobals());

const historyCalls = () =>
  vi
    .mocked(fetch)
    .mock.calls.map(([url]) => String(url))
    .filter((url) => url.includes('/history'));

function show(alerts: ActiveAlert[] | null = [warm]) {
  return render(
    <Dashboard
      snapshot={snapshot}
      unit="C"
      timezone={null}
      stale={false}
      alerts={alerts}
      onOpenEvents={() => {}}
    />,
  );
}

it('states the room in words, with the limit from the sensor settings and how long', async () => {
  show();
  expect(
    screen.getByRole('heading', { name: 'Exhaust is running warm' }),
  ).toBeInTheDocument();
  expect(
    await screen.findByText(
      '29.4 °C, above its 28.0 °C limit for 12 minutes. The other probe is in range.',
    ),
  ).toBeInTheDocument();
  const probes = screen.getByRole('list', { name: 'Probes' });
  const exhaust = within(probes).getAllByRole('listitem')[1];
  expect(exhaust).toHaveTextContent('Too warm');
  expect(
    within(exhaust).getByRole('img', {
      name: 'Exhaust: 29.4 °C, range 15.0 to 28.0 °C',
    }),
  ).toBeInTheDocument();
  expect(screen.getByText('Updated 3 seconds ago')).toBeInTheDocument();
});

it('charts the last day from one history query, not one per live update', async () => {
  const view = show();
  expect(
    await screen.findByRole('img', {
      name: /five-minute average temperature of 2 probes/,
    }),
  ).toBeInTheDocument();
  expect(historyCalls()).toHaveLength(1);
  // A new live snapshot (every five seconds) does not query history again.
  view.rerender(
    <Dashboard
      snapshot={{ ...snapshot, sequence: 9, uptime_ms: 3_605_000 }}
      unit="C"
      timezone={null}
      stale={false}
      alerts={[warm]}
      onOpenEvents={() => {}}
    />,
  );
  await act(async () => {});
  expect(historyCalls()).toHaveLength(1);
  // Each probe is a line in its series colour, and Exhaust's limit is drawn because it is out.
  const lines = document.querySelectorAll('path[data-series]');
  expect(Array.from(lines).map((line) => line.getAttribute('stroke'))).toEqual([
    tokens.light.series[0],
    tokens.light.series[1],
  ]);
  expect(screen.getByText('Exhaust limit 28.0 °C')).toBeInTheDocument();
});

it('switches the range with one new query, and hides a probe from the chart', async () => {
  show();
  await screen.findByRole('img', { name: /2 probes/ });
  fireEvent.click(screen.getByRole('button', { name: '7 days' }));
  expect(
    await screen.findByRole('heading', { name: 'Last 7 days' }),
  ).toBeInTheDocument();
  expect(historyCalls()).toHaveLength(2);
  const from = new URL(historyCalls()[1], 'http://device').searchParams.get(
    'from',
  );
  expect(Number(from)).toBe(0); // seven days back from one hour of uptime is the boot
  fireEvent.click(screen.getByRole('button', { name: 'Canopy' }));
  expect(screen.getByRole('button', { name: 'Canopy' })).toHaveAttribute(
    'aria-pressed',
    'false',
  );
  expect(document.querySelectorAll('path[data-series]')).toHaveLength(1);
});

it('shows active alerts beside the chart, or says there are none', async () => {
  const view = show();
  expect(screen.getByText('Exhaust is too warm')).toBeInTheDocument();
  expect(screen.getByText('1 active')).toBeInTheDocument();
  view.unmount();
  show([]);
  expect(screen.getByText('No active alerts')).toBeInTheDocument();
  expect(
    screen.getByRole('button', { name: 'See all events' }),
  ).toBeInTheDocument();
});

it('shows placeholders until the first snapshot arrives', () => {
  render(
    <Dashboard
      snapshot={null}
      unit="C"
      timezone={null}
      stale={false}
      alerts={null}
      onOpenEvents={() => {}}
    />,
  );
  expect(screen.getByLabelText('Loading live readings')).toBeInTheDocument();
  expect(screen.queryByRole('list', { name: 'Probes' })).toBeNull();
});
