import { fireEvent, render, screen } from '@testing-library/react';
import { beforeEach, afterEach, expect, it, vi } from 'vitest';
import SensorsView from './SensorsView';
const sensor = {
  rom_id: '2801000000000029',
  etag: '"simulator:2801000000000029:1"',
  revision: 1,
  name: 'Room',
  short_name: '',
  enabled: true,
  disconnect_alert: true,
  order: 0,
  calibration_c: 0,
  low_c: null,
  high_c: null,
  hysteresis_c: 0.5,
  persistence_seconds: 15,
  state: 'normal',
};
beforeEach(() =>
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => [sensor] }),
  ),
);
afterEach(() => vi.unstubAllGlobals());
it('submits ROM-keyed settings including negative calibration and disabled state', async () => {
  render(<SensorsView />);
  fireEvent.change(await screen.findByLabelText(/Sensor name/), {
    target: { value: 'Freezer' },
  });
  fireEvent.change(screen.getByLabelText(/Calibration offset/), {
    target: { value: '-1.25' },
  });
  fireEvent.click(screen.getByLabelText('Monitoring enabled'));
  vi.mocked(fetch).mockResolvedValueOnce({
    ok: true,
    json: async () => ({ status: 'queued' }),
  } as Response);
  fireEvent.click(screen.getByRole('button', { name: 'Save sensor settings' }));
  await screen.findByText(
    'Update queued. It applies with the next measurement cycle.',
  );
  const last = vi.mocked(fetch).mock.calls.at(-1)!;
  expect(last[0]).toBe('/api/v1/sensors/2801000000000029');
  expect(last[1]?.headers).toMatchObject({ 'If-Match': sensor.etag });
  expect(JSON.parse(last[1]?.body as string)).toMatchObject({
    name: 'Freezer',
    enabled: false,
    calibration_c: -1.25,
  });
  expect(JSON.parse(last[1]?.body as string)).not.toHaveProperty('rom_id');
});
it('rejects reversed thresholds before sending an update', async () => {
  render(<SensorsView />);
  await screen.findByLabelText(/Sensor name/);
  fireEvent.change(screen.getByLabelText(/Low threshold/), {
    target: { value: '30' },
  });
  fireEvent.change(screen.getByLabelText(/High threshold/), {
    target: { value: '20' },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Save sensor settings' }));
  await screen.findByText('Low threshold must be below high threshold.');
  expect(fetch).toHaveBeenCalledTimes(1);
});
it('rejects malformed configuration payloads without rendering an unsafe editor', async () => {
  vi.mocked(fetch).mockResolvedValueOnce({
    ok: true,
    json: async () => [{ ...sensor, rom_id: 'not-a-rom' }],
  } as Response);
  render(<SensorsView />);
  await screen.findByText('Invalid sensor configuration response');
  expect(
    screen.queryByRole('button', { name: 'Save sensor settings' }),
  ).not.toBeInTheDocument();
});
