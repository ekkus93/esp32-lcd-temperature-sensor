import { beforeEach, expect, it, vi } from 'vitest';
import { getAlerts } from './alerts';
import { authState } from './auth';
import { getDisplaySettings } from './settings';
import { networkProgress, wifiScan } from './network';
import { parseSnapshot } from './client';
import { empty } from './fixtures';

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
  body = { enrolled: true, signed_in: true, csrf: 'a'.repeat(48) };
  expect(await authState()).toEqual(body);
  // An anonymous visitor is told they are not signed in and gets no CSRF token.
  body = { enrolled: true, signed_in: false };
  expect(await authState()).toEqual(body);
  body = { enrolled: true };
  await expect(authState()).rejects.toThrow('Invalid authentication state');
  body = { enrolled: true, signed_in: true, csrf: 3 };
  await expect(authState()).rejects.toThrow('Invalid authentication state');
});

it('keeps snapshot v1 strict and validates separately versioned probe diagnostics elsewhere', () => {
  expect(parseSnapshot(empty)).toEqual(empty);
  expect(() =>
    parseSnapshot({
      ...empty,
      probe_scan: {
        enabled: true,
        complete: true,
        discovered_count: 6,
        overflow: true,
      },
    }),
  ).toThrow('Invalid device snapshot');
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

it('reads display settings from the public response and from the owner response', async () => {
  const display = {
    schema_version: 1,
    units: 'F',
    display_mode: 'basic',
    overview_seconds: 20,
    trends_seconds: 10,
    lookback_minutes: 60,
    timezone: 'America/Los_Angeles',
  };
  // Anonymous: no etag, revision or NTP server.
  body = display;
  expect(await getDisplaySettings()).toEqual({
    units: 'F',
    timezone: 'America/Los_Angeles',
  });
  // Signed in: the full owner response carries the same display fields.
  body = {
    ...display,
    etag: '"b:settings:1"',
    revision: 1,
    writable: true,
    ntp_server: 'pool.ntp.org',
  };
  expect(await getDisplaySettings()).toEqual({
    units: 'F',
    timezone: 'America/Los_Angeles',
  });
  body = { ...display, units: 'K' };
  await expect(getDisplaySettings()).rejects.toThrow(
    'Invalid device settings response',
  );
});
