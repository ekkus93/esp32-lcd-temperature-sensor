import { act, fireEvent, screen } from '@testing-library/react';
import { renderThemed as render } from '../renderThemed';
import { afterEach, expect, it, vi } from 'vitest';
import type { ClockDiagnostics } from '../api/clock';
import DeviceSettings from './DeviceSettings';
const settings = {
  schema_version: 1,
  etag: '"test:settings:1"',
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
afterEach(() => {
  vi.useRealTimers();
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
});
// Requests are told apart by URL and method, never by order: the clock is polled every five
// seconds and can land before, during or after a save (review audit F8).
function routedDevice(patchAnswer: () => Promise<unknown>) {
  const clock: ClockDiagnostics = {
    schema_version: 1,
    synchronized: false,
    last_sync_utc_seconds: null,
    sync_age_ms: null,
  };
  return vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
    const path = String(input);
    const method = init?.method ?? 'GET';
    const body =
      path === '/api/v1/clock' && method === 'GET'
        ? Promise.resolve(clock)
        : path === '/api/v1/settings' && method === 'GET'
          ? Promise.resolve(settings)
          : path === '/api/v1/settings' && method === 'PATCH'
            ? patchAnswer()
            : Promise.reject(new Error(`unexpected ${method} ${path}`));
    return body.then(
      (json) => ({ ok: true, json: async () => json }) as Response,
    );
  });
}
const patches = () =>
  vi
    .mocked(fetch)
    .mock.calls.filter(
      ([url, init]) =>
        String(url) === '/api/v1/settings' && init?.method === 'PATCH',
    );
const clockPolls = () =>
  vi.mocked(fetch).mock.calls.filter(([url]) => String(url) === '/api/v1/clock')
    .length;

it('saves device preferences with an update precondition and applies shared settings', async () => {
  vi.useFakeTimers({ shouldAdvanceTime: true });
  try {
    const applied = vi.fn();
    let answer: (value: unknown) => void = () => {};
    vi.stubGlobal(
      'fetch',
      routedDevice(() => new Promise((resolve) => (answer = resolve))),
    );
    render(<DeviceSettings onApplied={applied} />);
    fireEvent.change(await screen.findByLabelText(/Overview duration/), {
      target: { value: '25' },
    });
    fireEvent.mouseDown(screen.getByLabelText('Temperature units'));
    fireEvent.click(
      await screen.findByRole('option', { name: 'Fahrenheit (°F)' }),
    );
    // A clock poll before the save...
    await act(async () => {
      await vi.advanceTimersByTimeAsync(5000);
    });
    const before = clockPolls();
    fireEvent.click(
      screen.getByRole('button', { name: 'Save device preferences' }),
    );
    // ...one while the save is waiting for the device...
    await act(async () => {
      await vi.advanceTimersByTimeAsync(5000);
    });
    expect(clockPolls()).toBeGreaterThan(before);
    expect(applied).not.toHaveBeenCalledWith('F', 'UTC');
    answer({
      ...settings,
      units: 'F',
      overview_seconds: 25,
      revision: 2,
      etag: '"test:settings:2"',
    });
    await screen.findByText('Device preferences saved.');
    // ...and one after it.
    await act(async () => {
      await vi.advanceTimersByTimeAsync(5000);
    });
    expect(applied).toHaveBeenLastCalledWith('F', 'UTC');
    expect(patches()).toHaveLength(1);
    const [, init] = patches()[0];
    expect(init?.headers).toMatchObject({ 'If-Match': settings.etag });
    const sent = JSON.parse(init?.body as string);
    expect(sent).toMatchObject({ overview_seconds: 25, units: 'F' });
    for (const field of ['revision', 'etag', 'writable', 'schema_version'])
      expect(sent).not.toHaveProperty(field);
  } finally {
    vi.useRealTimers();
  }
});
it('prevents writes when existing device settings are preserved read-only', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ ...settings, writable: false }),
    }),
  );
  render(<DeviceSettings onApplied={vi.fn()} />);
  expect(
    await screen.findByRole('button', { name: 'Save device preferences' }),
  ).toBeDisabled();
  expect(
    screen.getByRole('combobox', { name: 'Device timezone' }),
  ).toHaveAttribute('aria-disabled', 'true');
  expect(screen.getByLabelText(/Overview duration/)).toBeDisabled();
});

it('exposes independent read-only LCD mode, dwell times, and trend lookback', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => settings }),
  );
  render(<DeviceSettings onApplied={vi.fn()} />);
  expect(await screen.findByText(/The LCD is read-only/)).toBeInTheDocument();
  expect(
    screen.getByRole('combobox', { name: 'LCD display mode' }),
  ).toHaveTextContent('Basic — Overview');
  expect(screen.getByLabelText(/Overview duration/)).toHaveValue(20);
  expect(screen.getByLabelText(/Trends duration/)).toHaveValue(10);
  expect(screen.getByLabelText(/LCD history lookback/)).toHaveValue(60);
});

it('shows an honest unknown clock state and a UTC sync age after synchronization', async () => {
  const diagnostics: ClockDiagnostics = {
    schema_version: 1,
    synchronized: false,
    last_sync_utc_seconds: null,
    sync_age_ms: null,
  };
  vi.stubGlobal(
    'fetch',
    vi.fn((input: RequestInfo | URL) => {
      const path = String(input);
      return Promise.resolve({
        ok: true,
        json: async () =>
          path === '/api/v1/clock'
            ? diagnostics
            : path === '/api/v1/settings'
              ? settings
              : {
                  ...settings,
                  schema_version: 1,
                  etag: settings.etag,
                  revision: 1,
                },
      } as Response);
    }),
  );
  const first = render(<DeviceSettings onApplied={vi.fn()} />);
  expect(
    await screen.findByText('Clock has not synchronized since this boot.'),
  ).toBeInTheDocument();
  first.unmount();
  diagnostics.synchronized = true;
  diagnostics.last_sync_utc_seconds = 1_798_000_000;
  diagnostics.sync_age_ms = 65_000;
  render(<DeviceSettings onApplied={vi.fn()} />);
  expect(
    await screen.findByText(
      /Last successful NTP sync: .* UTC · 65 seconds ago/,
    ),
  ).toBeInTheDocument();
});

it('renames the device network name, lowercased, and sends it with the other settings', async () => {
  vi.stubGlobal(
    'fetch',
    routedDevice(async () => ({
      ...settings,
      hostname: 'garage-1',
      revision: 2,
      etag: '"test:settings:2"',
    })),
  );
  render(<DeviceSettings onApplied={vi.fn()} />);
  const field = await screen.findByLabelText(/Network name/);
  expect(field).toHaveValue('temperature-monitor');
  fireEvent.change(field, { target: { value: 'Garage-1' } });
  expect(field).toHaveValue('garage-1');
  fireEvent.click(
    screen.getByRole('button', { name: 'Save device preferences' }),
  );
  await screen.findByText('Device preferences saved.');
  expect(patches()).toHaveLength(1);
  expect(JSON.parse(patches()[0][1]?.body as string)).toMatchObject({
    hostname: 'garage-1',
  });
});

it.each(['-bad', 'bad-', 'has space', 'under_score', 'a.b'])(
  'rejects the network name "%s" before sending anything',
  async (value) => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({ ok: true, json: async () => settings }),
    );
    render(<DeviceSettings onApplied={vi.fn()} />);
    fireEvent.change(await screen.findByLabelText(/Network name/), {
      target: { value },
    });
    fireEvent.click(
      screen.getByRole('button', { name: 'Save device preferences' }),
    );
    expect(
      await screen.findByText(
        /Use 1 to 63 lowercase letters, digits or hyphens/,
      ),
    ).toBeInTheDocument();
    expect(
      vi
        .mocked(fetch)
        .mock.calls.every(([, options]) => options?.method !== 'PATCH'),
    ).toBe(true);
  },
);

// An empty name is stopped by the browser's own required-field check.
it('does not send an empty network name', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => settings }),
  );
  render(<DeviceSettings onApplied={vi.fn()} />);
  fireEvent.change(await screen.findByLabelText(/Network name/), {
    target: { value: '' },
  });
  fireEvent.click(
    screen.getByRole('button', { name: 'Save device preferences' }),
  );
  await new Promise((resolve) => setTimeout(resolve, 50));
  expect(
    vi
      .mocked(fetch)
      .mock.calls.every(([, options]) => options?.method !== 'PATCH'),
  ).toBe(true);
});
