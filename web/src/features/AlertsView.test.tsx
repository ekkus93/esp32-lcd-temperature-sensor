import { render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import AlertsView from './AlertsView';

afterEach(() => vi.unstubAllGlobals());

it('distinguishes active bus/probe faults, pages recovery events and shows retention', async () => {
  const active = [
    { state: 'bus_fault' },
    { rom_id: '2801000000000029', name: 'Freezer', state: 'disconnected' },
  ];
  const first = {
    active,
    events: [
      {
        sequence: 2,
        boot_id: '0000000000000001',
        kind: 'sensor_state',
        rom_id: '2801000000000029',
        previous: 'normal',
        state: 'disconnected',
        uptime_ms: 10000,
        utc_seconds: null,
        detail: 0,
      },
    ],
    next_after: 2,
    has_more: true,
    oldest_available: 2,
    event_log_healthy: true,
  };
  const second = {
    active,
    events: [
      {
        sequence: 3,
        boot_id: '0000000000000001',
        kind: 'sensor_state',
        rom_id: '2801000000000029',
        previous: 'disconnected',
        state: 'normal',
        uptime_ms: 20000,
        utc_seconds: null,
        detail: 0,
      },
    ],
    next_after: 3,
    has_more: false,
    oldest_available: 2,
    event_log_healthy: true,
  };
  vi.stubGlobal(
    'fetch',
    vi
      .fn()
      .mockResolvedValueOnce({ ok: true, json: async () => first })
      .mockResolvedValue({ ok: true, json: async () => second }),
  );
  render(<AlertsView />);
  expect(await screen.findByText(/Device: bus fault/)).toBeInTheDocument();
  expect(screen.getByText(/Freezer: disconnected/)).toBeInTheDocument();
  expect(screen.getByText(/disconnected → normal/)).toBeInTheDocument();
  expect(
    screen.getByText(/Some older events have left the bounded event history/),
  ).toBeInTheDocument();
  expect(
    screen.getByText(/stored in a bounded local journal across reboots/),
  ).toBeInTheDocument();
  expect(vi.mocked(fetch).mock.calls).toHaveLength(2);
  expect(vi.mocked(fetch).mock.calls[1][0]).toContain(
    '/alerts?after=2&limit=50',
  );
});
