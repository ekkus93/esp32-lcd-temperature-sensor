import { fireEvent, render, screen } from '@testing-library/react';
import { beforeEach, expect, it, vi } from 'vitest';
import App from './App';
import { empty } from './api/fixtures';
beforeEach(() => {
  vi.stubGlobal('matchMedia', () => ({ matches: false }));
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => empty }),
  );
});
it('shows empty sensors honestly without a fabricated reading', async () => {
  render(<App />);
  expect(await screen.findByText('No probes detected')).toBeInTheDocument();
  expect(screen.queryByText('0.0 °C')).not.toBeInTheDocument();
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
  expect(screen.getByText('-18.5 °C')).toBeInTheDocument();
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
    await screen.findByText('Probe 5 insulated utility room'),
  ).toBeInTheDocument();
  expect(screen.getByText('24.0 °C')).toBeInTheDocument();
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
  expect(screen.getByText('Warm room')).toBeInTheDocument();
  expect(screen.getByText('Healthy room')).toBeInTheDocument();
  expect(screen.getByText('Disabled probe')).toBeInTheDocument();
  expect(screen.getByText('Missing probe')).toBeInTheDocument();
  expect(screen.getByText('high')).toBeInTheDocument();
  expect(screen.getByText('disabled')).toBeInTheDocument();
  expect(screen.getByText('disconnected')).toBeInTheDocument();
  expect(screen.getByText('21.0 °C')).toBeInTheDocument();
});
it('marks unavailable device data visibly', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockRejectedValue(new Error('Live connection lost')),
  );
  render(<App />);
  expect(await screen.findByText(/Live connection lost/)).toBeInTheDocument();
});
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
  fireEvent.click(screen.getByRole('button', { name: 'Settings' }));
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
it('explains unavailable storage without implying healthy logging', async () => {
  render(<App />);
  fireEvent.click(screen.getByRole('button', { name: 'Settings' }));
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
