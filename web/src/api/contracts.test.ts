import { beforeEach, expect, it, vi } from 'vitest';
import { getAlerts } from './alerts';
import { authState } from './auth';
import { networkProgress, wifiScan } from './network';

let body: unknown;
beforeEach(() => {
  vi.stubGlobal(
    'fetch',
    vi.fn(async () => ({
      ok: true,
      status: 200,
      json: async () => body,
    })),
  );
});

it('validates enrollment and session state from the device contract', async () => {
  body = { enrolled: true, csrf: 'a'.repeat(48) };
  expect(await authState()).toEqual(body);
  body = { enrolled: true, csrf: 3 };
  await expect(authState()).rejects.toThrow('Invalid authentication state');
});

it('validates active bus and sensor alerts with bounded event history', async () => {
  body = {
    active: [
      { state: 'bus_fault' },
      { rom_id: '2801000000000029', name: 'Freezer', state: 'disconnected' },
    ],
    events: [
      {
        sequence: 1,
        boot_id: '0123456789abcdef',
        kind: 'sensor_state',
        rom_id: null,
        previous: 'normal',
        state: 'bus_fault',
        uptime_ms: 5000,
        utc_seconds: null,
        detail: 0,
      },
    ],
    next_after: 1,
    has_more: false,
    oldest_available: 1,
    event_log_healthy: true,
  };
  expect(await getAlerts()).toEqual(body);
  expect(vi.mocked(fetch).mock.calls.at(-1)?.[0]).toContain('/alerts?limit=50');
  await getAlerts(5, 10);
  expect(vi.mocked(fetch).mock.calls.at(-1)?.[0]).toContain(
    '/alerts?after=5&limit=10',
  );
  body = {
    active: [{ state: 'mystery' }],
    events: [],
    next_after: 0,
    has_more: false,
    oldest_available: null,
    event_log_healthy: true,
  };
  await expect(getAlerts()).rejects.toThrow('Invalid alert response');
});

it('validates Wi-Fi status and bounded scan results', async () => {
  body = {
    state: 'setup',
    ip: '192.168.4.1',
    provisioning: true,
    saved_credentials: false,
    attempt: 'idle',
    remaining_seconds: 0,
    error: '',
  };
  expect((await networkProgress()).state).toBe('setup');
  body = {
    scanning: false,
    networks: [{ ssid: 'Example', rssi: -42, channel: 6, secure: true }],
  };
  expect((await wifiScan()).networks[0].ssid).toBe('Example');
  body = {
    scanning: false,
    networks: [{ ssid: 'Example', rssi: -42, channel: 15, secure: true }],
  };
  await expect(wifiScan()).rejects.toThrow('Invalid network scan response');
});
