import { act, fireEvent, render, screen } from '@testing-library/react';
import { beforeEach, expect, it, vi } from 'vitest';
import App from './App';
import { empty } from './api/fixtures';
vi.mock('./features/HistoryView', () => ({
  default: ({ timezone }: { timezone: string | null }) => (
    <div>Selected zone: {timezone ?? 'not loaded'}</div>
  ),
}));
beforeEach(() => {
  history.replaceState({}, '', '/');
  vi.stubGlobal('matchMedia', () => ({ matches: false }));
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => empty }),
  );
});
it('shows empty sensors honestly without a fabricated reading', async () => {
  render(<App />);
  expect(await screen.findByText('No probes detected')).toBeInTheDocument();
  expect(
    screen.getByText(/Explore five-minute averages, minima and maxima/),
  ).toBeInTheDocument();
  expect(
    screen.queryByText(/No history available yet/),
  ).not.toBeInTheDocument();
  expect(screen.queryByText('32.0 °F')).not.toBeInTheDocument();
});
it('shows the age of the latest valid probe reading', async () => {
  const snapshot = {
    ...empty,
    uptime_ms: 10000,
    sensors: [
      {
        rom_id: '2801000000000029',
        name: 'Freezer',
        state: 'normal',
        temperature_c: -18.5,
        last_seen_ms: 5000,
        enabled: true,
        order: 0,
      },
    ],
  };
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => snapshot }),
  );
  render(<App />);
  expect(await screen.findByText('Updated 5 seconds ago')).toBeInTheDocument();
  expect(screen.getByText('-1.3 °F')).toBeInTheDocument();
});
it('shows all five probes with long labels without dropping readings', async () => {
  const sensors = Array.from({ length: 5 }, (_, index) => ({
    rom_id: `28${String(index + 1).padStart(14, '0')}`,
    name: `Probe ${index + 1} insulated utility room`,
    state: 'normal',
    temperature_c: 20 + index,
    last_seen_ms: 0,
    enabled: true,
    order: index,
  }));
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ ...empty, sensors }),
    }),
  );
  render(<App />);
  expect(
    await screen.findByText('Probe 5 insulated utility room'),
  ).toBeInTheDocument();
  expect(screen.getByText('75.2 °F')).toBeInTheDocument();
});
it('keeps healthy sensors visible alongside bus and per-probe fault states', async () => {
  const snapshot = {
    ...empty,
    bus_fault: true,
    uptime_ms: 20000,
    sensors: [
      {
        rom_id: '2801000000000029',
        name: 'Warm room',
        state: 'high',
        temperature_c: 31,
        last_seen_ms: 20000,
        enabled: true,
        order: 0,
      },
      {
        rom_id: '280200000000002a',
        name: 'Healthy room',
        state: 'normal',
        temperature_c: 21,
        last_seen_ms: 20000,
        enabled: true,
        order: 1,
      },
      {
        rom_id: '280300000000002b',
        name: 'Disabled probe',
        state: 'disabled',
        temperature_c: null,
        last_seen_ms: null,
        enabled: false,
        order: 2,
      },
      {
        rom_id: '280400000000002c',
        name: 'Missing probe',
        state: 'disconnected',
        temperature_c: null,
        last_seen_ms: null,
        enabled: true,
        order: 3,
      },
    ],
  };
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => snapshot }),
  );
  render(<App />);
  expect(
    await screen.findByText(/Shared sensor bus fault/),
  ).toBeInTheDocument();
  expect(screen.getByText('Warm room')).toBeInTheDocument();
  expect(screen.getByText('Healthy room')).toBeInTheDocument();
  expect(screen.getByText('Disabled probe')).toBeInTheDocument();
  expect(screen.getByText('Missing probe')).toBeInTheDocument();
  expect(screen.getByText('high')).toBeInTheDocument();
  expect(screen.getByText('disabled')).toBeInTheDocument();
  expect(screen.getByText('disconnected')).toBeInTheDocument();
  expect(screen.getByText('69.8 °F')).toBeInTheDocument();
});
it('reports excess and incomplete physical probe discovery', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockImplementation(async (input: RequestInfo | URL) => ({
      ok: true,
      json: async () =>
        String(input).endsWith('/probe-scan')
          ? {
              schema_version: 1,
              enabled: true,
              complete: false,
              discovered_count: 6,
              overflow: true,
            }
          : empty,
    })),
  );
  render(<App />);
  expect(await screen.findByText(/at least 6/)).toBeInTheDocument();
  expect(
    screen.getByText(/Previously selected probes remain prioritized/),
  ).toBeInTheDocument();
});
it('marks unavailable device data visibly', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockRejectedValue(new Error('Live connection lost')),
  );
  render(<App />);
  expect(await screen.findByText(/Live connection lost/)).toBeInTheDocument();
});
it('shows storage capacity and five-minute policy in Settings diagnostics', async () => {
  const snapshot = {
    ...empty,
    logging: {
      ...empty.logging,
      healthy: true,
      storage_available: true,
      storage_total_bytes: 4 * 1024 * 1024,
      storage_used_bytes: 1024 * 1024,
    },
  };
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => snapshot }),
  );
  render(<App />);
  fireEvent.click(screen.getByRole('button', { name: 'Settings' }));
  expect(
    await screen.findByText('Storage and diagnostics'),
  ).toBeInTheDocument();
  expect(
    screen.getByText(/Firmware 0\.1\.0 · ota_0 · OTA valid/),
  ).toBeInTheDocument();
  expect(screen.getByText('1.00 MiB used of 4.00 MiB')).toBeInTheDocument();
  expect(
    screen.getByText(/Aggregates are recorded every 300 seconds/),
  ).toBeInTheDocument();
});

it('applies a saved timezone to History without reloading the app', async () => {
  let settings = {
    schema_version: 1,
    etag: '"device:settings:1"',
    revision: 1,
    writable: true,
    units: 'C',
    display_mode: 'basic',
    overview_seconds: 20,
    trends_seconds: 10,
    lookback_minutes: 60,
    timezone: 'UTC',
    ntp_server: 'pool.ntp.org',
  };
  vi.stubGlobal(
    'fetch',
    vi
      .fn()
      .mockImplementation(
        async (input: RequestInfo | URL, init?: RequestInit) => {
          const path = String(input);
          if (path.endsWith('/probe-scan'))
            return {
              ok: true,
              json: async () => ({
                schema_version: 1,
                enabled: false,
                complete: false,
                discovered_count: 0,
                overflow: false,
              }),
            };
          if (path.endsWith('/settings')) {
            if (init?.method === 'PATCH') {
              settings = {
                ...settings,
                ...JSON.parse(init.body as string),
                revision: 2,
                etag: '"device:settings:2"',
              };
            }
            return { ok: true, json: async () => settings };
          }
          return { ok: true, json: async () => empty };
        },
      ),
  );
  const app = render(<App />);
  fireEvent.click(screen.getByRole('button', { name: 'Settings' }));
  fireEvent.mouseDown(await screen.findByLabelText('Device timezone'));
  fireEvent.click(
    await screen.findByRole('option', { name: 'America/Los Angeles' }),
  );
  fireEvent.click(
    screen.getByRole('button', { name: 'Save device preferences' }),
  );
  await screen.findByText('Device preferences saved.');
  fireEvent.click(screen.getByRole('button', { name: 'History' }));
  expect(
    await screen.findByText('Selected zone: America/Los_Angeles'),
  ).toBeInTheDocument();
  app.unmount();
  render(<App />);
  fireEvent.click(screen.getByRole('button', { name: 'History' }));
  expect(
    await screen.findByText('Selected zone: America/Los_Angeles'),
  ).toBeInTheDocument();
});

it('keeps the dashboard history link neutral across populated and unsynchronized snapshots', async () => {
  const snapshots = [
    {
      ...empty,
      uptime_ms: 600000,
      sensors: [
        {
          rom_id: '2801000000000029',
          name: 'Room',
          state: 'normal',
          temperature_c: 20,
          last_seen_ms: 600000,
          enabled: true,
          order: 0,
        },
      ],
    },
    {
      ...empty,
      boot_id: 'different-boot',
      uptime_ms: 300000,
      clock: { ...empty.clock, synchronized: false, utc_seconds: null },
    },
  ];
  for (const snapshot of snapshots) {
    history.replaceState({}, '', '/');
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({ ok: true, json: async () => snapshot }),
    );
    const view = render(<App />);
    expect(
      await screen.findByText(
        /Explore five-minute averages, minima and maxima/,
      ),
    ).toBeInTheDocument();
    view.unmount();
  }
});
it('explains unavailable storage without implying healthy logging', async () => {
  render(<App />);
  fireEvent.click(screen.getByRole('button', { name: 'Settings' }));
  expect(
    await screen.findByText('Storage and diagnostics'),
  ).toBeInTheDocument();
  expect(
    screen.getByText('Five-minute history: unavailable'),
  ).toBeInTheDocument();
  expect(
    screen.getByText(
      /Storage capacity is unavailable until the history filesystem mounts/,
    ),
  ).toBeInTheDocument();
});
it('keeps the last readings visible, marked stale, when the live connection drops', async () => {
  const sockets: { onclose: (() => void) | null }[] = [];
  class FakeSocket {
    onopen: (() => void) | null = null;
    onmessage: ((e: { data: unknown }) => void) | null = null;
    onclose: (() => void) | null = null;
    onerror: (() => void) | null = null;
    readyState = 0;
    constructor() {
      sockets.push(this);
    }
    close() {
      this.onclose?.();
    }
  }
  vi.stubGlobal('WebSocket', FakeSocket);
  const snapshot = {
    ...empty,
    uptime_ms: 10000,
    sensors: [
      {
        rom_id: '2801000000000029',
        name: 'Freezer',
        state: 'normal',
        temperature_c: -18.5,
        last_seen_ms: 5000,
        enabled: true,
        order: 0,
      },
    ],
  };
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => snapshot }),
  );
  render(<App />);
  expect(await screen.findByText('-1.3 °F')).toBeInTheDocument();
  await act(async () => {
    sockets[0].onclose?.();
  });
  expect(
    await screen.findByText(/Live connection lost; readings are stale/),
  ).toBeInTheDocument();
  // The reading stays on screen, flagged stale, instead of turning into a dash.
  expect(screen.getByText('-1.3 °F')).toBeInTheDocument();
  expect(screen.getByText('Stale')).toBeInTheDocument();
});
it('shows an error and retries when the display settings cannot be loaded', async () => {
  const snapshot = {
    ...empty,
    uptime_ms: 10000,
    sensors: [
      {
        rom_id: '2801000000000029',
        name: 'Freezer',
        state: 'normal',
        temperature_c: -18.5,
        last_seen_ms: 5000,
        enabled: true,
        order: 0,
      },
    ],
  };
  const settings = {
    schema_version: 1,
    etag: '"device:settings:1"',
    revision: 1,
    writable: true,
    units: 'C',
    display_mode: 'basic',
    overview_seconds: 20,
    trends_seconds: 10,
    lookback_minutes: 60,
    timezone: 'UTC',
    ntp_server: 'pool.ntp.org',
  };
  let settingsCalls = 0;
  vi.stubGlobal(
    'fetch',
    vi.fn().mockImplementation(async (input: RequestInfo | URL) => {
      const path = String(input);
      if (path.endsWith('/settings')) {
        settingsCalls += 1;
        if (settingsCalls === 1) throw new TypeError('Failed to fetch');
        return { ok: true, json: async () => settings };
      }
      if (path.endsWith('/probe-scan'))
        return {
          ok: true,
          json: async () => ({
            schema_version: 1,
            enabled: false,
            complete: false,
            discovered_count: 0,
            overflow: false,
          }),
        };
      return { ok: true, json: async () => snapshot };
    }),
  );
  render(<App />);
  // Until the real setting is known, the page says so instead of silently assuming Fahrenheit.
  expect(
    await screen.findByText(/Display settings could not be loaded/),
  ).toBeInTheDocument();
  // The retry succeeds: the saved unit applies and the notice goes away.
  expect(
    await screen.findByText('-18.5 °C', {}, { timeout: 6000 }),
  ).toBeInTheDocument();
  expect(
    screen.queryByText(/Display settings could not be loaded/),
  ).not.toBeInTheDocument();
});
