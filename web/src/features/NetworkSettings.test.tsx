import { fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import NetworkSettings from './NetworkSettings';
afterEach(() => vi.unstubAllGlobals());
it('scans asynchronously and selects an SSID without submitting credentials', async () => {
  vi.stubGlobal(
    'fetch',
    vi
      .fn()
      .mockResolvedValueOnce({ ok: true, json: async () => ({}) })
      .mockResolvedValueOnce({
        ok: true,
        json: async () => ({
          scanning: false,
          networks: [
            { ssid: 'Test WLAN', rssi: -45, channel: 6, secure: true },
          ],
        }),
      }),
  );
  render(<NetworkSettings />);
  fireEvent.click(screen.getByRole('button', { name: 'Scan for networks' }));
  fireEvent.click(await screen.findByRole('button', { name: /Test WLAN/ }));
  expect(screen.getByLabelText(/2.4 GHz network SSID/)).toHaveValue(
    'Test WLAN',
  );
  expect(fetch).toHaveBeenCalledTimes(2);
  expect(vi.mocked(fetch).mock.calls[0][0]).toBe('/api/v1/wifi/scan');
  expect(vi.mocked(fetch).mock.calls[0][1]?.method).toBe('POST');
});
it('keeps manual SSID entry available after a failed scan', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: false,
      status: 429,
      json: async () => ({ error: 'Scan busy' }),
    }),
  );
  render(<NetworkSettings />);
  fireEvent.click(screen.getByRole('button', { name: 'Scan for networks' }));
  await screen.findByText('Scan busy');
  expect(screen.getByLabelText(/2.4 GHz network SSID/)).toBeEnabled();
});
it('reports a failed candidate without claiming saved credentials were replaced', async () => {
  vi.stubGlobal(
    'fetch',
    vi
      .fn()
      .mockResolvedValueOnce({ ok: true, json: async () => ({}) })
      .mockResolvedValueOnce({
        ok: true,
        json: async () => ({
          state: 'setup',
          ip: '0.0.0.0',
          provisioning: true,
          saved_credentials: false,
          attempt: 'failed',
          remaining_seconds: 0,
          error: 'Network not found or incompatible security',
        }),
      }),
  );
  render(<NetworkSettings />);
  fireEvent.change(screen.getByLabelText(/2.4 GHz network SSID/), {
    target: { value: 'Test WLAN' },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Test and connect' }));
  await screen.findByText(
    /Previous credentials are preserved; you can retry/,
    {},
    { timeout: 3000 },
  );
  expect(
    screen.getByRole('button', { name: 'Test and connect' }),
  ).toBeEnabled();
});
