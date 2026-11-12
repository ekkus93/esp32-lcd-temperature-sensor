import { screen } from '@testing-library/react';
import { renderThemed as render } from '../renderThemed';
import { afterEach, expect, it, vi } from 'vitest';
import AlertsView from './AlertsView';

afterEach(() => vi.unstubAllGlobals());

it('shows bus, Wi-Fi and probe conditions as active alerts', async () => {
  const response = {
    active: [
      {
        state: 'bus_fault',
        since_uptime_ms: 5000,
        since_utc_seconds: null,
      },
      {
        state: 'wifi_disconnected',
        since_uptime_ms: 9000,
        since_utc_seconds: null,
      },
      {
        rom_id: '2801000000000029',
        name: 'Freezer',
        state: 'disconnected',
        since_uptime_ms: 12000,
        since_utc_seconds: null,
      },
    ],
    events: [],
    next_after: 0,
    has_more: false,
    oldest_available: null,
    event_log_healthy: true,
  };
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => response }),
  );
  render(<AlertsView timezone={null} />);
  expect(await screen.findByText('Sensor bus fault')).toBeInTheDocument();
  expect(screen.getByText('Wi-Fi disconnected')).toBeInTheDocument();
  expect(screen.getByText('Freezer is disconnected')).toBeInTheDocument();
  expect(screen.queryByText(/Device started/)).toBeNull();
  expect(screen.getAllByRole('listitem')).toHaveLength(3);
  expect(screen.getByText('Since boot +12 seconds')).toBeInTheDocument();
  expect(vi.mocked(fetch).mock.calls[0][0]).toContain('limit=1');
});

it('says so when nothing needs attention', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        active: [],
        events: [],
        next_after: 0,
        has_more: false,
        oldest_available: null,
        event_log_healthy: true,
      }),
    }),
  );
  render(<AlertsView timezone={null} />);
  expect(await screen.findByText('No active alerts')).toBeInTheDocument();
});

it('dates an active alert in the device timezone once the clock is trusted', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        active: [
          {
            rom_id: '2801000000000029',
            name: 'Freezer',
            state: 'high',
            since_uptime_ms: 60000,
            since_utc_seconds: 1767268800,
          },
        ],
        events: [],
        next_after: 0,
        has_more: false,
        oldest_available: null,
        event_log_healthy: true,
      }),
    }),
  );
  render(<AlertsView timezone="America/Los_Angeles" />);
  expect(await screen.findByText('Freezer is too warm')).toBeInTheDocument();
  expect(
    await screen.findByText(
      /^Since Jan 1, 2026, 4:00 AM PST \(America\/Los_Angeles\)$/,
    ),
  ).toBeInTheDocument();
});
