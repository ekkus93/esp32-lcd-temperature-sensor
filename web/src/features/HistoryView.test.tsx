import { fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import HistoryView from './HistoryView';
import { empty } from '../api/fixtures';
import { parseSnapshot } from '../api/client';

afterEach(() => vi.unstubAllGlobals());

it('shows selectable history series for probes no longer in the live snapshot', async () => {
  const rom = '2801000000000029';
  const response = {
    schema_version: 1,
    simulated: false,
    truncated: false,
    time_basis: 'uptime',
    points: [
      {
        boot_id: '0123456789abcdef',
        rom_id: rom,
        start_ms: 300000,
        end_ms: 600000,
        utc_seconds: null,
        average_c: 4,
        minimum_c: 3,
        maximum_c: 5,
        count: 60,
        partial: false,
      },
    ],
    gaps: [],
  };
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => response }),
  );
  render(
    <HistoryView
      snapshot={parseSnapshot({ ...empty, uptime_ms: 7200000 })}
      unit="C"
    />,
  );
  const checkbox = await screen.findByRole('checkbox', {
    name: new RegExp(rom),
  });
  expect(
    await screen.findByRole('img', { name: /Probe 0029/ }),
  ).toBeInTheDocument();
  expect(screen.getByRole('button', { name: 'Zoom out' })).toBeDisabled();
  fireEvent.click(screen.getByRole('button', { name: 'Zoom in' }));
  expect(screen.getByRole('button', { name: 'Zoom out' })).toBeEnabled();
  expect(screen.getByRole('button', { name: 'Earlier' })).toBeEnabled();
  fireEvent.click(screen.getByRole('button', { name: 'Earlier' }));
  expect(screen.getByRole('button', { name: 'Later' })).toBeEnabled();
  fireEvent.click(screen.getByRole('button', { name: 'Zoom in' }));
  fireEvent.click(screen.getByRole('button', { name: 'Zoom in' }));
  expect(screen.getByRole('button', { name: 'Zoom in' })).toBeDisabled();
  fireEvent.click(screen.getByRole('button', { name: 'Reset zoom' }));
  expect(screen.getByRole('button', { name: 'Zoom out' })).toBeDisabled();
  fireEvent.click(checkbox);
  expect(
    screen.queryByRole('img', { name: /Probe 0029/ }),
  ).not.toBeInTheDocument();
});

it('explains an empty range and unsynchronized boot-relative time', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        schema_version: 1,
        simulated: false,
        truncated: false,
        time_basis: 'uptime',
        points: [],
        gaps: [],
      }),
    }),
  );
  render(<HistoryView snapshot={parseSnapshot(empty)} unit="C" />);
  expect(
    await screen.findByText('No history available in this range.'),
  ).toBeInTheDocument();
  expect(
    screen.getByText(
      /Clock unsynchronized: showing elapsed time for this boot only/,
    ),
  ).toBeInTheDocument();
});
