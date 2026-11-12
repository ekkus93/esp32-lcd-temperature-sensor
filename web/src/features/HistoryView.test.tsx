import { fireEvent, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import HistoryView from './HistoryView';
import { empty } from '../api/fixtures';
import { parseSnapshot } from '../api/client';
import { renderThemed as render } from '../renderThemed';
import { tokens } from '../theme/tokens.generated';

afterEach(() => vi.unstubAllGlobals());

it('shows selectable history series for probes no longer in the live snapshot', async () => {
  const rom = '2801000000000029';
  const response = {
    schema_version: 1,
    simulated: false,
    truncated: false,
    time_basis: 'uptime',
    points: [
      {
        boot_id: '0123456789abcdef',
        rom_id: rom,
        start_ms: 300000,
        end_ms: 600000,
        utc_seconds: null,
        average_c: 4,
        minimum_c: 3,
        maximum_c: 5,
        count: 60,
        partial: false,
      },
    ],
    gaps: [],
  };
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => response }),
  );
  render(
    <HistoryView
      snapshot={parseSnapshot({ ...empty, uptime_ms: 7200000 })}
      unit="C"
      timezone={null}
    />,
  );
  const checkbox = await screen.findByRole('checkbox', {
    name: new RegExp(rom),
  });
  expect(
    await screen.findByRole('img', {
      name: /Five-minute average temperature chart for 1 probe/,
    }),
  ).toBeInTheDocument();
  expect(screen.getByRole('button', { name: 'Zoom out' })).toBeDisabled();
  fireEvent.click(screen.getByRole('button', { name: 'Zoom in' }));
  expect(screen.getByRole('button', { name: 'Zoom out' })).toBeEnabled();
  expect(screen.getByRole('button', { name: 'Earlier' })).toBeEnabled();
  fireEvent.click(screen.getByRole('button', { name: 'Earlier' }));
  expect(screen.getByRole('button', { name: 'Later' })).toBeEnabled();
  fireEvent.click(screen.getByRole('button', { name: 'Zoom in' }));
  fireEvent.click(screen.getByRole('button', { name: 'Zoom in' }));
  expect(screen.getByRole('button', { name: 'Zoom in' })).toBeDisabled();
  fireEvent.click(screen.getByRole('button', { name: 'Reset zoom' }));
  expect(screen.getByRole('button', { name: 'Zoom out' })).toBeDisabled();
  fireEvent.click(checkbox);
  expect(
    screen.queryByRole('img', {
      name: /Five-minute average temperature chart/,
    }),
  ).not.toBeInTheDocument();
}, 10000);

it('explains an empty range and unsynchronized boot-relative time', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        schema_version: 1,
        simulated: false,
        truncated: false,
        time_basis: 'uptime',
        points: [],
        gaps: [],
      }),
    }),
  );
  render(
    <HistoryView snapshot={parseSnapshot(empty)} unit="C" timezone={null} />,
  );
  expect(
    await screen.findByText('No history available in this range.'),
  ).toBeInTheDocument();
  expect(
    screen.getByText(
      /Clock unsynchronized: showing elapsed time for this boot only/,
    ),
  ).toBeInTheDocument();
});

it('visibly shades missing intervals in the selected history series', async () => {
  const rom = '2801000000000029';
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        schema_version: 1,
        simulated: false,
        truncated: false,
        time_basis: 'uptime',
        points: [
          {
            boot_id: '0123456789abcdef',
            rom_id: rom,
            start_ms: 300000,
            end_ms: 600000,
            utc_seconds: null,
            average_c: 4,
            minimum_c: 3,
            maximum_c: 5,
            count: 60,
            partial: false,
          },
          {
            boot_id: '0123456789abcdef',
            rom_id: rom,
            start_ms: 1200000,
            end_ms: 1500000,
            utc_seconds: null,
            average_c: 4.5,
            minimum_c: 4,
            maximum_c: 5,
            count: 60,
            partial: false,
          },
        ],
        gaps: [
          {
            boot_id: '0123456789abcdef',
            rom_id: rom,
            start_ms: 600000,
            end_ms: 1200000,
          },
        ],
      }),
    }),
  );
  render(
    <HistoryView
      snapshot={parseSnapshot({ ...empty, uptime_ms: 7200000 })}
      unit="C"
      timezone="America/Los_Angeles"
    />,
  );
  expect(
    await screen.findByRole('img', {
      name: /Five-minute average temperature chart/,
    }),
  ).toContainHTML('Missing data: 10 min to 20 min since boot (10 min missing)');
});

const twoFarPoints = (rom: string) => ({
  schema_version: 1,
  simulated: false,
  truncated: false,
  time_basis: 'uptime',
  points: [0, 3000000].map((start) => ({
    boot_id: '0123456789abcdef',
    rom_id: rom,
    start_ms: start,
    end_ms: start + 300000,
    utc_seconds: null,
    average_c: 4,
    minimum_c: 3,
    maximum_c: 5,
    count: 60,
    partial: false,
  })),
  gaps: [],
});

it('explains a zoomed view that contains no data', async () => {
  const rom = '2801000000000029';
  vi.stubGlobal(
    'fetch',
    vi
      .fn()
      .mockResolvedValue({ ok: true, json: async () => twoFarPoints(rom) }),
  );
  render(
    <HistoryView
      snapshot={parseSnapshot({ ...empty, uptime_ms: 7200000 })}
      unit="C"
      timezone={null}
    />,
  );
  await screen.findByRole('img', {
    name: /Five-minute average temperature chart/,
  });
  expect(screen.queryByText(/No data in this zoomed view/)).toBeNull();
  for (let i = 0; i < 3; i++)
    fireEvent.click(screen.getByRole('button', { name: 'Zoom in' }));
  expect(
    await screen.findByText(
      'No data in this zoomed view. Zoom out, or use Earlier and Later.',
    ),
  ).toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Reset zoom' }));
  expect(screen.queryByText(/No data in this zoomed view/)).toBeNull();
});

it('loads history again once the clock synchronizes', async () => {
  const fetchMock = vi.fn().mockResolvedValue({
    ok: true,
    json: async () => twoFarPoints('2801000000000029'),
  });
  vi.stubGlobal('fetch', fetchMock);
  const unsynchronized = parseSnapshot(empty);
  const synchronized = parseSnapshot({
    ...empty,
    clock: { utc: '2026-11-05T17:00:00Z', synchronized: true },
  });
  const view = render(
    <HistoryView snapshot={unsynchronized} unit="C" timezone={null} />,
  );
  await screen.findByRole('img', {
    name: /Five-minute average temperature chart/,
  });
  const before = fetchMock.mock.calls.length;
  // A new snapshot with the same sync state does not reload.
  view.rerender(
    <HistoryView
      snapshot={{ ...unsynchronized, sequence: 5 }}
      unit="C"
      timezone={null}
    />,
  );
  expect(fetchMock.mock.calls.length).toBe(before);
  // The first clock sync changes the time basis, so history is requested again.
  view.rerender(
    <HistoryView snapshot={synchronized} unit="C" timezone={null} />,
  );
  await vi.waitFor(() =>
    expect(fetchMock.mock.calls.length).toBeGreaterThan(before),
  );
});

it('exposes the selected range and history errors to assistive technology', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockRejectedValue(new TypeError('Failed to fetch')),
  );
  render(
    <HistoryView snapshot={parseSnapshot(empty)} unit="C" timezone={null} />,
  );
  // The selected range is announced as pressed; the others are not.
  expect(screen.getByRole('button', { name: '24h' })).toHaveAttribute(
    'aria-pressed',
    'true',
  );
  expect(screen.getByRole('button', { name: '1h' })).toHaveAttribute(
    'aria-pressed',
    'false',
  );
  fireEvent.click(screen.getByRole('button', { name: '7d' }));
  expect(screen.getByRole('button', { name: '7d' })).toHaveAttribute(
    'aria-pressed',
    'true',
  );
  // A failed load is an alert, not a silent line of text.
  expect(await screen.findByRole('alert')).toHaveTextContent(
    'Could not reach the device',
  );
});

it('draws every selected probe on one chart, each in its own colour', async () => {
  const first = '2801000000000029';
  const second = '2801000000000030';
  const point = (rom: string, start: number, average: number) => ({
    boot_id: '0123456789abcdef',
    rom_id: rom,
    start_ms: start,
    end_ms: start + 300000,
    utc_seconds: null,
    average_c: average,
    minimum_c: average - 0.5,
    maximum_c: average + 0.5,
    count: 60,
    partial: false,
  });
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        schema_version: 1,
        simulated: false,
        truncated: false,
        time_basis: 'uptime',
        points: [
          point(first, 0, 4),
          point(first, 300000, 4.5),
          point(second, 0, 20),
          point(second, 300000, 21),
        ],
        gaps: [],
      }),
    }),
  );
  const snapshot = parseSnapshot({
    ...empty,
    uptime_ms: 7200000,
    sensors: [
      {
        rom_id: first,
        name: 'Sensor 1',
        state: 'normal',
        temperature_c: 4.5,
        last_seen_ms: 7200000,
        enabled: true,
        order: 0,
      },
      {
        rom_id: second,
        name: 'Sensor 2',
        state: 'normal',
        temperature_c: 21,
        last_seen_ms: 7200000,
        enabled: true,
        order: 1,
      },
    ],
  });
  const { container } = render(
    <HistoryView snapshot={snapshot} unit="C" timezone={null} />,
  );
  expect(
    await screen.findByRole('img', {
      name: 'Five-minute average temperature chart for 2 probes',
    }),
  ).toBeInTheDocument();
  expect(
    screen.getAllByRole('img', { name: /temperature chart/ }),
  ).toHaveLength(1);
  const lines = container.querySelectorAll('polyline');
  expect(lines).toHaveLength(2);
  // Coloured by display position, in the LCD's hue order (blue, then yellow).
  expect(lines[0].getAttribute('stroke')).toBe(tokens.light.series[0]);
  expect(lines[1].getAttribute('stroke')).toBe(tokens.light.series[1]);
});

it('lists probes in their position order, not by ROM ID', async () => {
  const later = '2801000000000001'; // lower ROM ID, but position 1
  const first = '2802000000000002';
  const point = (rom: string) => ({
    boot_id: '0123456789abcdef',
    rom_id: rom,
    start_ms: 0,
    end_ms: 300000,
    utc_seconds: null,
    average_c: 4,
    minimum_c: 3,
    maximum_c: 5,
    count: 60,
    partial: false,
  });
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        schema_version: 1,
        simulated: false,
        truncated: false,
        time_basis: 'uptime',
        points: [point(later), point(first)],
        gaps: [],
      }),
    }),
  );
  const sensor = (rom_id: string, name: string, order: number) => ({
    rom_id,
    name,
    state: 'normal' as const,
    temperature_c: 4,
    last_seen_ms: 7200000,
    enabled: true,
    order,
  });
  const snapshot = parseSnapshot({
    ...empty,
    uptime_ms: 7200000,
    sensors: [sensor(first, 'Sensor 1', 0), sensor(later, 'Sensor 2', 1)],
  });
  render(<HistoryView snapshot={snapshot} unit="C" timezone={null} />);
  const boxes = await screen.findAllByRole('checkbox');
  expect(boxes.map((box) => box.closest('label')?.textContent)).toEqual([
    `Sensor 1 · ${first}`,
    `Sensor 2 · ${later}`,
  ]);
});

it('runs one line through a missing interval and still shades it', async () => {
  const rom = '2801000000000029';
  const point = (start: number) => ({
    boot_id: '0123456789abcdef',
    rom_id: rom,
    start_ms: start,
    end_ms: start + 300000,
    utc_seconds: null,
    average_c: 4,
    minimum_c: 3,
    maximum_c: 5,
    count: 60,
    partial: false,
  });
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        schema_version: 1,
        simulated: false,
        truncated: false,
        time_basis: 'uptime',
        points: [point(0), point(1800000)],
        gaps: [
          {
            boot_id: '0123456789abcdef',
            rom_id: rom,
            start_ms: 300000,
            end_ms: 1800000,
          },
        ],
      }),
    }),
  );
  const { container } = render(
    <HistoryView
      snapshot={parseSnapshot({ ...empty, uptime_ms: 7200000 })}
      unit="C"
      timezone={null}
    />,
  );
  await screen.findByRole('img', { name: /average temperature chart/ });
  const lines = container.querySelectorAll('polyline');
  expect(lines).toHaveLength(1);
  expect(lines[0].getAttribute('points')?.split(' ')).toHaveLength(2);
  expect(container.querySelector('rect title')?.textContent).toContain(
    'Missing data',
  );
});
