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
  render(<AlertsView timezone={null} />);
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

it('formats synchronized events in the configured device timezone', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        active: [],
        events: [
          {
            sequence: 1,
            boot_id: '0000000000000001',
            kind: 'reboot',
            rom_id: null,
            previous: 'normal',
            state: 'normal',
            uptime_ms: 0,
            utc_seconds: 1767268800,
            detail: 0,
          },
        ],
        next_after: 1,
        has_more: false,
        oldest_available: 1,
        event_log_healthy: true,
      }),
    }),
  );
  render(<AlertsView timezone="America/Los_Angeles" />);
  expect(
    await screen.findByText(
      /Jan 1, 2026, 4:00 AM PST \(America\/Los_Angeles\): Device started/,
    ),
  ).toBeInTheDocument();
});

it('keeps unsynchronized events boot-relative when the device timezone is unknown', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        active: [],
        events: [
          {
            sequence: 1,
            boot_id: '0123456789abcdef',
            kind: 'reboot',
            rom_id: null,
            previous: 'normal',
            state: 'normal',
            uptime_ms: 12500,
            utc_seconds: null,
            detail: 0,
          },
        ],
        next_after: 1,
        has_more: false,
        oldest_available: 1,
        event_log_healthy: true,
      }),
    }),
  );
  render(<AlertsView timezone={null} />);
  expect(
    await screen.findByText(/Boot 01234567 · \+13 seconds: Device started/),
  ).toBeInTheDocument();
  expect(
    screen.getByText(/Device timezone unavailable; synchronized times use UTC/),
  ).toBeInTheDocument();
});
