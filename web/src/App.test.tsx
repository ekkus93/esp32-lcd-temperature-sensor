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
// A probe's entry on the Dashboard's live readings panel.
async function panelEntry(name: string) {
  const list = await screen.findByRole('list', { name: 'Probes' });
  const item = Array.from(list.querySelectorAll('li')).find((li) =>
    li.textContent?.startsWith(name),
  );
  if (!item) throw new Error(`No panel entry for ${name}`);
  return item;
}

it('shows empty sensors honestly without a fabricated reading', async () => {
  render(<App />);
  expect(await screen.findByText('No probes detected')).toBeInTheDocument();
  expect(
    screen.getByRole('heading', { name: 'Last 24 hours' }),
  ).toBeInTheDocument();
  expect(
    screen.queryByText(/No history available yet/),
  ).not.toBeInTheDocument();
  expect(screen.queryByText('32.0')).not.toBeInTheDocument();
  expect(screen.getByRole('list', { name: 'Probes' }).children).toHaveLength(0);
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
  expect(await panelEntry('Freezer')).toHaveTextContent(/-1\.3\s*°F/);
  // The ROM ID is not shown on the Dashboard.
  expect(screen.queryByText(/2801000000000029/)).toBeNull();
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
    await screen.findAllByText('Probe 5 insulated utility room'),
  ).not.toHaveLength(0);
  expect(screen.getByRole('list', { name: 'Probes' }).children).toHaveLength(5);
  expect(await panelEntry('Probe 5 insulated utility room')).toHaveTextContent(
    /75\.2\s*°F/,
  );
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
  // The panel puts the bus fault first, and still shows each enabled probe and its state.
  expect(screen.getByText('The sensor bus has a fault')).toBeInTheDocument();
  expect(await panelEntry('Warm room')).toHaveTextContent('Too warm');
  expect(await panelEntry('Healthy room')).toHaveTextContent(/69\.8\s*°F/);
  expect(await panelEntry('Missing probe')).toHaveTextContent('Disconnected');
  expect(await panelEntry('Missing probe')).toHaveTextContent('—');
  // A disabled probe is not monitored, so it is not on the live panel.
  expect(screen.queryByText('Disabled probe')).toBeNull();
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
  fireEvent.click(screen.getByRole('link', { name: 'Settings' }));
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
  fireEvent.click(screen.getByRole('link', { name: 'Settings' }));
  fireEvent.mouseDown(await screen.findByLabelText('Device timezone'));
  fireEvent.click(
    await screen.findByRole('option', { name: 'America/Los Angeles' }),
  );
  fireEvent.click(
    screen.getByRole('button', { name: 'Save device preferences' }),
  );
  await screen.findByText('Device preferences saved.');
  fireEvent.click(screen.getByRole('link', { name: 'History' }));
  expect(
    await screen.findByText('Selected zone: America/Los_Angeles'),
  ).toBeInTheDocument();
  app.unmount();
  render(<App />);
  fireEvent.click(screen.getByRole('link', { name: 'History' }));
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
      clock: { utc: null, synchronized: false },
    },
  ];
  for (const snapshot of snapshots) {
    history.replaceState({}, '', '/');
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({ ok: true, json: async () => snapshot }),
    );
    const view = render(<App />);
    // The chart card is there whatever the clock and probes, and points to all events.
    expect(
      await screen.findByRole('heading', { name: 'Last 24 hours' }),
    ).toBeInTheDocument();
    expect(
      screen.getByRole('button', { name: 'See all events' }),
    ).toBeInTheDocument();
    view.unmount();
  }
});
it('explains unavailable storage without implying healthy logging', async () => {
  render(<App />);
  fireEvent.click(screen.getByRole('link', { name: 'Settings' }));
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
  expect(await panelEntry('Freezer')).toHaveTextContent(/-1\.3\s*°F/);
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
  // The reading stays on screen, and the panel says it is out of date, instead of a dash.
  expect(await panelEntry('Freezer')).toHaveTextContent(/-1\.3\s*°F/);
  expect(screen.getByText('Readings are out of date')).toBeInTheDocument();
  expect(screen.getByText('Live connection lost')).toBeInTheDocument();
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
    await screen.findByText('°C', {}, { timeout: 6000 }),
  ).toBeInTheDocument();
  expect(await panelEntry('Freezer')).toHaveTextContent(/-18\.5\s*°C/);
  expect(
    screen.queryByText(/Display settings could not be loaded/),
  ).not.toBeInTheDocument();
});
it('remembers the chosen theme across reloads', async () => {
  const first = render(<App />);
  fireEvent.click(screen.getByRole('button', { name: 'Switch to dark theme' }));
  expect(
    screen.getByRole('button', { name: 'Switch to light theme' }),
  ).toBeInTheDocument();
  first.unmount();
  render(<App />);
  expect(
    screen.getByRole('button', { name: 'Switch to light theme' }),
  ).toBeInTheDocument();
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
          // Sensor configurations and storage state are public, as on the device
          // (route_access.cpp); only changes need a session.
          if (path.endsWith('/sensors') || path.endsWith('/sensors/storage')) {
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
  fireEvent.click(screen.getByRole('link', { name: 'Alerts' }));
  expect(await screen.findByText('No active alerts')).toBeInTheDocument();
  expect(screen.queryByLabelText(/Administrator password/)).toBeNull();
});

it('shows Alerts and Events as tabs of one page, Alerts first and by default', async () => {
  stubDevice({ signedIn: false });
  render(<App />);
  await screen.findByText('No probes detected');
  // Events is no longer a page of its own.
  expect(screen.queryByRole('link', { name: 'Events' })).toBeNull();
  fireEvent.click(screen.getByRole('link', { name: 'Alerts' }));
  expect(
    await screen.findByRole('tab', { name: 'Alerts', selected: true }),
  ).toBeInTheDocument();
  expect(screen.getAllByRole('tab').map((tab) => tab.textContent)).toEqual([
    'Alerts',
    'Events',
  ]);
  expect(location.pathname).toBe('/alerts');
  fireEvent.click(screen.getByRole('tab', { name: 'Events' }));
  expect(await screen.findByText('No recent events.')).toBeInTheDocument();
  expect(location.pathname).toBe('/events');
  expect(screen.queryByText('No active alerts')).toBeNull();
});

it('opens the Events tab from an old /events link', async () => {
  stubDevice({ signedIn: false });
  history.replaceState({}, '', '/events');
  render(<App />);
  expect(
    await screen.findByRole('tab', { name: 'Events', selected: true }),
  ).toBeInTheDocument();
  expect(await screen.findByText('No recent events.')).toBeInTheDocument();
  expect(screen.getByRole('heading', { level: 1 })).toHaveTextContent('Alerts');
});

it('asks for the password on the Sensors page and shows the forms after signing in', async () => {
  const calls = stubDevice({ signedIn: false });
  render(<App />);
  await screen.findByText('No probes detected');
  fireEvent.click(screen.getByRole('link', { name: 'Sensors' }));
  expect(await screen.findByText('Administrator sign in')).toBeInTheDocument();
  expect(
    screen.getByText(/Readings and history do not need a password/),
  ).toBeInTheDocument();
  // The sensor editor did not load while anonymous: it asks for the storage state on opening.
  expect(calls.some((call) => call.endsWith('/sensors/storage'))).toBe(false);
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
  fireEvent.click(screen.getByRole('link', { name: 'Settings' }));
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
  fireEvent.click(await screen.findByRole('link', { name: 'Settings' }));
  fireEvent.click(await screen.findByRole('tab', { name: 'System' }));
  expect(await screen.findByText('http://garage-2.local')).toBeInTheDocument();
  first.unmount();
  history.replaceState({}, '', '/');
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => empty }),
  );
  render(<App />);
  fireEvent.click(await screen.findByRole('link', { name: 'Settings' }));
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
  fireEvent.click(screen.getByRole('link', { name: 'Settings' }));
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

it('navigates with five linked destinations and marks the current page', async () => {
  stubDevice({ signedIn: false });
  render(<App />);
  await screen.findByText('No probes detected');
  const nav = screen.getByRole('navigation', { name: 'Main navigation' });
  const links = Array.from(nav.querySelectorAll('a'));
  expect(links.map((link) => link.textContent)).toEqual([
    'Dashboard',
    'History',
    'Sensors',
    'Alerts',
    'Settings',
  ]);
  expect(links.map((link) => link.getAttribute('href'))).toEqual([
    '/',
    '/history',
    '/sensors',
    '/alerts',
    '/settings',
  ]);
  expect(links[0]).toHaveAttribute('aria-current', 'page');
  fireEvent.click(links[1]);
  expect(location.pathname).toBe('/history');
  expect(links[1]).toHaveAttribute('aria-current', 'page');
  expect(links[0]).not.toHaveAttribute('aria-current');
  // The old overline label is gone; the page title is the only heading in the bar.
  expect(screen.queryByText(/local temperature monitor/i)).toBeNull();
});

it('counts active alerts on the Alerts destination', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn(async (url: string) => ({
      ok: true,
      json: async () =>
        url.includes('/alerts')
          ? {
              active: [
                {
                  rom_id: '2801000000000029',
                  name: 'Exhaust',
                  state: 'high',
                  since_uptime_ms: 1000,
                  since_utc_seconds: null,
                },
                {
                  state: 'wifi_disconnected',
                  since_uptime_ms: 2000,
                  since_utc_seconds: null,
                },
              ],
              events: [],
              next_after: 0,
              has_more: false,
              oldest_available: null,
              event_log_healthy: true,
            }
          : empty,
    })),
  );
  render(<App />);
  expect(
    await screen.findByRole('link', { name: 'Alerts, 2 active' }),
  ).toBeInTheDocument();
});

it('shows only the device conditions that need attention, and the network name', async () => {
  const healthy = {
    ...empty,
    simulated: false,
    clock: { utc: '2026-11-12T10:00:00Z', synchronized: true },
    wifi: { state: 'connected', mdns_hostname: 'grow-room' },
    logging: { ...empty.logging, healthy: true },
  };
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => healthy }),
  );
  const view = render(<App />);
  expect(await screen.findByText('grow-room.local')).toBeInTheDocument();
  expect(screen.queryByRole('list', { name: 'Device conditions' })).toBeNull();
  view.unmount();

  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => empty }),
  );
  render(<App />);
  const list = await screen.findByRole('list', { name: 'Device conditions' });
  expect(
    Array.from(list.querySelectorAll('li')).map((item) => item.textContent),
  ).toEqual([
    'Wi-Fi offline',
    'Clock not synchronized',
    'History logging unavailable',
    'Simulated readings',
  ]);
});
