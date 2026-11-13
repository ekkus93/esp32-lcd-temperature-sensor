import { fireEvent, screen } from '@testing-library/react';
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
afterEach(() => vi.unstubAllGlobals());
it('saves device preferences with an update precondition and applies shared settings', async () => {
  const applied = vi.fn();
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => settings }),
  );
  render(<DeviceSettings onApplied={applied} />);
  fireEvent.change(await screen.findByLabelText(/Overview duration/), {
    target: { value: '25' },
  });
  fireEvent.mouseDown(screen.getByLabelText('Temperature units'));
  fireEvent.click(
    await screen.findByRole('option', { name: 'Fahrenheit (°F)' }),
  );
  vi.mocked(fetch).mockResolvedValueOnce({
    ok: true,
    json: async () => ({
      ...settings,
      units: 'F',
      overview_seconds: 25,
      revision: 2,
      etag: '"test:settings:2"',
    }),
  } as Response);
  fireEvent.click(
    screen.getByRole('button', { name: 'Save device preferences' }),
  );
  await screen.findByText('Device preferences saved.');
  expect(applied).toHaveBeenLastCalledWith('F', 'UTC');
  const last = vi.mocked(fetch).mock.calls.at(-1)!;
  expect(last[0]).toBe('/api/v1/settings');
  expect(last[1]?.headers).toMatchObject({ 'If-Match': settings.etag });
  expect(JSON.parse(last[1]?.body as string)).toMatchObject({
    overview_seconds: 25,
    units: 'F',
  });
  expect(JSON.parse(last[1]?.body as string)).not.toHaveProperty('revision');
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
    vi.fn().mockResolvedValue({ ok: true, json: async () => settings }),
  );
  render(<DeviceSettings onApplied={vi.fn()} />);
  const field = await screen.findByLabelText(/Network name/);
  expect(field).toHaveValue('temperature-monitor');
  fireEvent.change(field, { target: { value: 'Garage-1' } });
  expect(field).toHaveValue('garage-1');
  vi.mocked(fetch).mockResolvedValueOnce({
    ok: true,
    json: async () => ({
      ...settings,
      hostname: 'garage-1',
      revision: 2,
      etag: '"test:settings:2"',
    }),
  } as Response);
  fireEvent.click(
    screen.getByRole('button', { name: 'Save device preferences' }),
  );
  await screen.findByText('Device preferences saved.');
  const last = vi.mocked(fetch).mock.calls.at(-1)!;
  expect(JSON.parse(last[1]?.body as string)).toMatchObject({
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
