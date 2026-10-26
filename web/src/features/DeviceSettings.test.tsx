import { fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
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
};
afterEach(() => vi.unstubAllGlobals());
it('saves device preferences with an update precondition and applies the shared units', async () => {
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
  expect(applied).toHaveBeenLastCalledWith('F');
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
});
