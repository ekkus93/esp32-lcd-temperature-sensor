import { act, fireEvent, screen, within } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import Dashboard from './Dashboard';
import { parseSnapshot } from '../../api/client';
import { empty } from '../../api/fixtures';
import { renderThemed as render } from '../../renderThemed';
import { tokens } from '../../theme/tokens.generated';
import type { ActiveAlert } from '../alertText';
import type { Snapshot } from '../../api/generated';

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

// Review audit F6: a history answer is drawn only for the boot, clock and range it was asked for.
type Pending = {
  url: string;
  resolve: (body: unknown) => void;
  reject: (error: Error) => void;
};
function controlledHistory() {
  const pending: Pending[] = [];
  vi.stubGlobal(
    'fetch',
    vi.fn((input: RequestInfo | URL) => {
      const url = String(input);
      if (!url.includes('/history'))
        return Promise.resolve({
          ok: true,
          json: async () => [
            config(CANOPY, 'Canopy', 0, 18, 28),
            config(EXHAUST, 'Exhaust', 1, 15, 28),
          ],
        });
      return new Promise((resolve) =>
        pending.push({
          url,
          resolve: (body) => resolve({ ok: true, json: async () => body }),
          reject: () =>
            resolve({
              ok: false,
              status: 503,
              json: async () => ({ error: 'History unavailable or busy' }),
            }),
        }),
      );
    }),
  );
  return pending;
}
const withBoot = (boot: string, uptime: number, utc: string | null = null) =>
  parseSnapshot({
    ...snapshot,
    boot_id: boot,
    uptime_ms: uptime,
    clock: { utc, synchronized: utc !== null },
  });
const answer = (average: number, basis: 'uptime' | 'utc' = 'uptime') => ({
  ...history,
  time_basis: basis,
  points: [0, 1, 2, 3].map((i) => ({
    ...point(CANOPY, 3_000_000 + i * 300_000, average),
    utc_seconds: basis === 'utc' ? 1_800_000_000 + i * 300 : null,
    start_ms:
      basis === 'utc'
        ? (1_800_000_000 + i * 300) * 1000
        : 3_000_000 + i * 300_000,
    end_ms:
      basis === 'utc'
        ? (1_800_000_000 + i * 300 + 300) * 1000
        : 3_300_000 + i * 300_000,
  })),
});
function dashboard(snap: Snapshot) {
  return (
    <Dashboard
      snapshot={snap}
      unit="C"
      timezone={null}
      stale={false}
      alerts={[]}
      onOpenEvents={() => {}}
    />
  );
}
const lines = () => document.querySelectorAll('path[data-series]');
const settle = () => act(async () => {});

it('does not draw the old boot’s history on the new boot’s axis', async () => {
  const pending = controlledHistory();
  const view = render(dashboard(withBoot('aaaaaaaaaaaaaaaa', 3_600_000)));
  await settle();
  pending[0].resolve(answer(24));
  await settle();
  expect(lines().length).toBeGreaterThan(0);
  // A reboot whose uptime overlaps the old one: the old answer is not drawn while the new one loads.
  view.rerender(dashboard(withBoot('bbbbbbbbbbbbbbbb', 3_600_000)));
  await settle();
  expect(historyCalls()).toHaveLength(2);
  expect(new URL(pending[1].url, 'http://d').searchParams.get('boot_id')).toBe(
    'bbbbbbbbbbbbbbbb',
  );
  expect(lines()).toHaveLength(0);
  expect(screen.getByText('Loading history…')).toBeInTheDocument();
  pending[1].resolve(answer(30));
  await settle();
  expect(lines().length).toBeGreaterThan(0);
});

it('does not reuse uptime history once the clock is first synchronized', async () => {
  const pending = controlledHistory();
  const view = render(dashboard(withBoot('aaaaaaaaaaaaaaaa', 3_600_000)));
  await settle();
  pending[0].resolve(answer(24));
  await settle();
  expect(lines().length).toBeGreaterThan(0);
  view.rerender(
    dashboard(withBoot('aaaaaaaaaaaaaaaa', 3_605_000, '2027-01-15T08:05:00Z')),
  );
  await settle();
  expect(historyCalls()).toHaveLength(2);
  expect(new URL(pending[1].url, 'http://d').searchParams.has('boot_id')).toBe(
    false,
  );
  expect(lines()).toHaveLength(0);
  // An answer on the wrong clock is refused, not reinterpreted.
  pending[1].resolve(answer(24, 'uptime'));
  await settle();
  expect(lines()).toHaveLength(0);
  expect(screen.getByRole('alert')).toHaveTextContent(
    'History unavailable: The device answered for a different clock.',
  );
});

it('ignores a late answer for the previous range', async () => {
  const pending = controlledHistory();
  render(dashboard(withBoot('aaaaaaaaaaaaaaaa', 3_600_000)));
  await settle();
  fireEvent.click(screen.getByRole('button', { name: '7 days' }));
  await settle();
  expect(historyCalls()).toHaveLength(2);
  pending[1].resolve(answer(30));
  await settle();
  const fresh = lines()[0].getAttribute('d');
  pending[0].resolve(answer(10)); // the 24-hour answer arrives last
  await settle();
  expect(lines().length).toBeGreaterThan(0);
  expect(lines()[0].getAttribute('d')).toBe(fresh);
});

it('keeps a chart whose refresh failed, marked and on its own axis, then recovers', async () => {
  vi.useFakeTimers();
  try {
    const pending = controlledHistory();
    const first = withBoot('aaaaaaaaaaaaaaaa', 3_600_000);
    const view = render(dashboard(first));
    await settle();
    pending[0].resolve(answer(24));
    await settle();
    const original = lines()[0].getAttribute('d');
    // Five minutes later the refresh fails.
    view.rerender(dashboard(withBoot('aaaaaaaaaaaaaaaa', 3_900_000)));
    await act(async () => {
      await vi.advanceTimersByTimeAsync(300_000);
    });
    expect(historyCalls()).toHaveLength(2);
    pending[1].reject(new Error('busy'));
    await settle();
    expect(screen.getByRole('alert')).toHaveTextContent(
      /History could not be refreshed .* The chart shows the last answer/,
    );
    // Still drawn, on the bounds it was fetched for (not slid to the new uptime).
    expect(lines().length).toBeGreaterThan(0);
    expect(lines()[0].getAttribute('d')).toBe(original);
    // Try again succeeds: the notice goes and the chart follows the live clock again.
    fireEvent.click(screen.getByRole('button', { name: 'Try again' }));
    await settle();
    expect(historyCalls()).toHaveLength(3);
    pending[2].resolve(answer(24));
    await settle();
    expect(screen.queryByRole('alert')).toBeNull();
    expect(lines()[0].getAttribute('d')).not.toBe(original);
  } finally {
    vi.useRealTimers();
  }
});

it('says when there is no history to show after a failure on a new range', async () => {
  const pending = controlledHistory();
  render(dashboard(withBoot('aaaaaaaaaaaaaaaa', 3_600_000)));
  await settle();
  pending[0].resolve(answer(24));
  await settle();
  fireEvent.click(screen.getByRole('button', { name: '7 days' }));
  await settle();
  pending[1].reject(new Error('busy'));
  await settle();
  expect(lines()).toHaveLength(0);
  expect(screen.getByRole('alert')).toHaveTextContent('History unavailable');
  expect(screen.getByText('No chart to show.')).toBeInTheDocument();
});
