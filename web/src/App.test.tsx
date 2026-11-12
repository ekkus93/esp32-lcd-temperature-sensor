import { act, fireEvent, render, screen } from '@testing-library/react';
import { beforeEach, expect, it, vi } from 'vitest';
import App from './App';
import { otaStateText } from './otaState';
import { empty } from './api/fixtures';
vi.mock('./features/HistoryView', () => ({
  default: ({ timezone }: { timezone: string | null }) => (
    <div>Selected zone: {timezone ?? 'not loaded'}</div>
  ),
}));
// jsdom's own WebSocket would try a real connection; tests use an inert one unless they drive it.
class InertSocket {
  onopen: (() => void) | null = null;
  onmessage: ((e: { data: unknown }) => void) | null = null;
  onclose: (() => void) | null = null;
  onerror: (() => void) | null = null;
  readyState = 0;
  close() {}
}
beforeEach(() => {
  vi.stubGlobal('WebSocket', InertSocket);
  localStorage.clear();
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
  // The warning appears only after the problem has lasted ten seconds.
  expect(
    await screen.findByText(/Live connection lost/, {}, { timeout: 12000 }),
  ).toBeInTheDocument();
}, 20000);
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
  fireEvent.click(await screen.findByRole('tab', { name: 'System' }));
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
    hostname: 'temperature-monitor',
  };
  vi.stubGlobal(
    'fetch',
    vi
      .fn()
      .mockImplementation(
        async (input: RequestInfo | URL, init?: RequestInit) => {
          const path = String(input);
          if (path.endsWith('/auth/state'))
            return {
              ok: true,
              json: async () => ({
                enrolled: true,
                signed_in: true,
                csrf: 'c'.repeat(40),
              }),
            };
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
  fireEvent.click(await screen.findByRole('tab', { name: 'System' }));
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
  // The feed stays down: the reconnect's reload fails too, so the readings really are stale.
  vi.mocked(fetch).mockRejectedValue(new Error('Live connection lost'));
  await act(async () => {
    sockets[0].onclose?.();
  });
  expect(
    await screen.findByText(
      /Live connection lost; readings are stale/,
      {},
      { timeout: 12000 },
    ),
  ).toBeInTheDocument();
  // The reading stays on screen, flagged stale, instead of turning into a dash.
  expect(screen.getByText('-1.3 °F')).toBeInTheDocument();
  expect(screen.getByText('Stale')).toBeInTheDocument();
}, 20000);
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
    hostname: 'temperature-monitor',
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
it('remembers the chosen theme across reloads', async () => {
  const first = render(<App />);
  fireEvent.click(screen.getByRole('button', { name: 'Toggle dark theme' }));
  expect(
    screen.getByRole('button', { name: 'Toggle dark theme' }),
  ).toHaveTextContent('Light theme');
  first.unmount();
  render(<App />);
  expect(
    screen.getByRole('button', { name: 'Toggle dark theme' }),
  ).toHaveTextContent('Light theme');
});
it('announces a shared sensor bus fault as an alert', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ ...empty, bus_fault: true }),
    }),
  );
  render(<App />);
  expect(await screen.findByRole('alert')).toHaveTextContent(
    /Shared sensor bus fault/,
  );
});

// A device that serves public data and, depending on `signedIn`, owner data too.
function stubDevice(options: { signedIn: boolean }) {
  const state = { signedIn: options.signedIn };
  const owner = {
    schema_version: 1,
    etag: '"device:settings:1"',
    revision: 1,
    writable: true,
    units: 'F',
    display_mode: 'basic',
    overview_seconds: 20,
    trends_seconds: 10,
    lookback_minutes: 60,
    timezone: 'UTC',
    ntp_server: 'pool.ntp.org',
    hostname: 'temperature-monitor',
  };
  const calls: string[] = [];
  vi.stubGlobal(
    'fetch',
    vi
      .fn()
      .mockImplementation(
        async (input: RequestInfo | URL, init?: RequestInit) => {
          const path = String(input);
          calls.push(`${init?.method ?? 'GET'} ${path}`);
          const ok = (body: unknown) => ({
            ok: true,
            status: 200,
            json: async () => body,
          });
          const denied = () => ({
            ok: false,
            status: 401,
            json: async () => ({ error: 'Administrator session required' }),
          });
          if (path.endsWith('/auth/state'))
            return ok(
              state.signedIn
                ? { enrolled: true, signed_in: true, csrf: 'c'.repeat(40) }
                : { enrolled: true, signed_in: false },
            );
          if (path.endsWith('/auth/login')) {
            state.signedIn = true;
            return ok({ csrf: 'c'.repeat(40) });
          }
          if (path.endsWith('/auth/logout')) {
            state.signedIn = false;
            return ok({});
          }
          if (path.endsWith('/settings'))
            return state.signedIn
              ? ok(owner)
              : ok({
                  schema_version: 1,
                  units: 'F',
                  display_mode: 'basic',
                  overview_seconds: 20,
                  trends_seconds: 10,
                  lookback_minutes: 60,
                  timezone: 'UTC',
                });
          if (path.endsWith('/sensors') || path.endsWith('/sensors/storage')) {
            if (!state.signedIn) return denied();
            return ok(
              path.endsWith('/storage')
                ? { schema_version: 1, state: 'ready', writable: true }
                : [],
            );
          }
          if (path.endsWith('/probe-scan'))
            return ok({
              schema_version: 1,
              enabled: false,
              complete: false,
              discovered_count: 0,
              overflow: false,
            });
          if (path.includes('/alerts'))
            return ok({
              active: [],
              events: [],
              next_after: 0,
              has_more: false,
              oldest_available: null,
              event_log_healthy: true,
            });
          if (path.includes('/wifi/'))
            return state.signedIn ? ok({}) : denied();
          return ok(empty);
        },
      ),
  );
  return calls;
}

it('shows the dashboard and alerts without signing in', async () => {
  stubDevice({ signedIn: false });
  render(<App />);
  expect(await screen.findByText('No probes detected')).toBeInTheDocument();
  expect(screen.getByRole('button', { name: 'Sign in' })).toBeInTheDocument();
  expect(screen.queryByRole('button', { name: 'Sign out' })).toBeNull();
  fireEvent.click(screen.getByRole('button', { name: 'Alerts' }));
  expect(await screen.findByText('No active alerts.')).toBeInTheDocument();
  expect(screen.queryByLabelText(/Administrator password/)).toBeNull();
});

it('asks for the password on the Sensors page and shows the forms after signing in', async () => {
  const calls = stubDevice({ signedIn: false });
  render(<App />);
  await screen.findByText('No probes detected');
  fireEvent.click(screen.getByRole('button', { name: 'Sensors' }));
  expect(await screen.findByText('Administrator sign in')).toBeInTheDocument();
  expect(
    screen.getByText(/Readings and history do not need a password/),
  ).toBeInTheDocument();
  // The owner endpoints were not even requested while anonymous.
  expect(calls.some((call) => call.endsWith('/sensors'))).toBe(false);
  fireEvent.change(screen.getByLabelText(/Administrator password/), {
    target: { value: 'an-example-password' },
  });
  fireEvent.click(screen.getAllByRole('button', { name: 'Sign in' }).at(-1)!);
  expect(
    await screen.findByText(
      'No sensors discovered. Unused positions do not cause alarms.',
    ),
  ).toBeInTheDocument();
  expect(screen.getByRole('button', { name: 'Sign out' })).toBeInTheDocument();
});

it('keeps the Settings diagnostics public and gates the owner controls', async () => {
  stubDevice({ signedIn: false });
  render(<App />);
  await screen.findByText('No probes detected');
  fireEvent.click(screen.getByRole('button', { name: 'Settings' }));
  // Settings opens on Preferences, which asks for the password before showing the form.
  expect(await screen.findByText('Administrator sign in')).toBeInTheDocument();
  expect(
    screen.queryByRole('button', { name: 'Save device preferences' }),
  ).toBeNull();
  // The System tab shows the public diagnostics, and its controls stay behind sign-in.
  fireEvent.click(screen.getByRole('tab', { name: 'System' }));
  expect(
    await screen.findByText('Storage and diagnostics'),
  ).toBeInTheDocument();
  expect(screen.getByText('Administrator sign in')).toBeInTheDocument();
  expect(screen.queryByText('Restart device')).toBeNull();
});

it('groups Settings into tabs and opens the Wi-Fi tab from /setup', async () => {
  stubDevice({ signedIn: true });
  history.replaceState({}, '', '/setup');
  render(<App />);
  expect(
    await screen.findByRole('tab', { name: 'Wi-Fi', selected: true }),
  ).toBeInTheDocument();
  expect(
    await screen.findByRole('button', { name: 'Test and connect' }),
  ).toBeInTheDocument();
  expect(screen.queryByText('Change administrator password')).toBeNull();
  expect(screen.getAllByRole('tab').map((tab) => tab.textContent)).toEqual([
    'Preferences',
    'Wi-Fi',
    'System',
    'Password',
  ]);
  fireEvent.click(screen.getByRole('tab', { name: 'Password' }));
  expect(
    await screen.findByRole('button', {
      name: 'Change administrator password',
    }),
  ).toBeInTheDocument();
  expect(screen.queryByRole('button', { name: 'Test and connect' })).toBeNull();
  history.replaceState({}, '', '/');
});

it('signs out from the app bar and returns to public reading', async () => {
  const calls = stubDevice({ signedIn: true });
  render(<App />);
  fireEvent.click(await screen.findByRole('button', { name: 'Sign out' }));
  expect(
    await screen.findByRole('button', { name: 'Sign in' }),
  ).toBeInTheDocument();
  expect(calls).toContain('POST /api/v1/auth/logout');
  // Public data keeps showing.
  expect(screen.getByText('No probes detected')).toBeInTheDocument();
});

it('shows the network name in the public Settings diagnostics, or says it is unavailable', async () => {
  const named = {
    ...empty,
    wifi: { state: 'connected', mdns_hostname: 'garage-2' },
  };
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => named }),
  );
  const first = render(<App />);
  fireEvent.click(await screen.findByRole('button', { name: 'Settings' }));
  fireEvent.click(await screen.findByRole('tab', { name: 'System' }));
  expect(await screen.findByText('http://garage-2.local')).toBeInTheDocument();
  first.unmount();
  history.replaceState({}, '', '/');
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => empty }),
  );
  render(<App />);
  fireEvent.click(await screen.findByRole('button', { name: 'Settings' }));
  fireEvent.click(await screen.findByRole('tab', { name: 'System' }));
  expect(
    await screen.findByText(/Network name unavailable/),
  ).toBeInTheDocument();
});

it('says in words when new firmware is being checked', async () => {
  const snapshot = {
    ...empty,
    firmware: {
      ...empty.firmware,
      partition: 'ota_1',
      ota_state: 'pending_verify',
    },
  };
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => snapshot }),
  );
  render(<App />);
  fireEvent.click(screen.getByRole('button', { name: 'Settings' }));
  fireEvent.click(await screen.findByRole('tab', { name: 'System' }));
  expect(
    await screen.findByText(
      /ota_1 · OTA new firmware being checked \(it rolls back if the check fails\)/,
    ),
  ).toBeInTheDocument();
});

it.each([
  ['valid', 'valid'],
  ['new', 'new firmware being checked (it rolls back if the check fails)'],
  ['invalid', 'rejected by its health check'],
  ['aborted', 'update aborted'],
  [undefined, 'unknown'],
])('describes the update state %s', (state, text) => {
  expect(otaStateText(state)).toBe(text);
});
