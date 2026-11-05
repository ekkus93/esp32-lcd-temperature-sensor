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
    vi.fn().mockImplementation(async (input: RequestInfo | URL) => ({
      ok: true,
      json: async () =>
        String(input).endsWith('/sensors/storage')
          ? { schema_version: 1, state: 'ready', writable: true }
          : [sensor],
    })),
  ),
);
afterEach(() => {
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
  vi.useRealTimers();
});
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
it('explains the empty managed-sensor list', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockImplementation(async (input: RequestInfo | URL) => ({
      ok: true,
      json: async () =>
        String(input).endsWith('/sensors/storage')
          ? { schema_version: 1, state: 'ready', writable: true }
          : [],
    })),
  );
  render(<SensorsView />);
  expect(
    await screen.findByText(
      'No sensors discovered. Unused positions do not cause alarms.',
    ),
  ).toBeInTheDocument();
});
it('renders all five sensor editors with maximum-length names', async () => {
  const sensors = Array.from({ length: 5 }, (_, index) => ({
    ...sensor,
    rom_id: `28${String(index + 1).padStart(14, '0')}`,
    etag: `"simulator:${index}:1"`,
    name: `${index + 1}${'x'.repeat(31)}`,
    order: index,
  }));
  vi.stubGlobal(
    'fetch',
    vi.fn().mockImplementation(async (input: RequestInfo | URL) => ({
      ok: true,
      json: async () =>
        String(input).endsWith('/sensors/storage')
          ? { schema_version: 1, state: 'ready', writable: true }
          : sensors,
    })),
  );
  render(<SensorsView />);
  const editors = await screen.findAllByLabelText(/Sensor name/);
  expect(editors).toHaveLength(5);
  expect(editors.map((editor) => (editor as HTMLInputElement).value)).toEqual(
    sensors.map((item) => item.name),
  );
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
  expect(fetch).toHaveBeenCalledTimes(2);
  expect(
    vi
      .mocked(fetch)
      .mock.calls.every(([, options]) => options?.method !== 'PATCH'),
  ).toBe(true);
});
it.each(['', 'abc'])(
  'rejects display position "%s" with a field error and sends nothing',
  async (value) => {
    render(<SensorsView />);
    const field = await screen.findByLabelText(/Display position/);
    fireEvent.change(field, { target: { value } });
    fireEvent.click(
      screen.getByRole('button', { name: 'Save sensor settings' }),
    );
    expect(
      await screen.findByText('Enter a whole number from 1 to 5.'),
    ).toBeInTheDocument();
    expect(
      vi
        .mocked(fetch)
        .mock.calls.every(([, options]) => options?.method !== 'PATCH'),
    ).toBe(true);
  },
);
// Out-of-range and fractional positions are stopped by the browser's own min/max/step check.
it.each(['0', '6', '2.5'])(
  'does not send display position "%s"',
  async (value) => {
    render(<SensorsView />);
    const field = await screen.findByLabelText(/Display position/);
    fireEvent.change(field, { target: { value } });
    fireEvent.click(
      screen.getByRole('button', { name: 'Save sensor settings' }),
    );
    await new Promise((resolve) => setTimeout(resolve, 50));
    expect(
      vi
        .mocked(fetch)
        .mock.calls.every(([, options]) => options?.method !== 'PATCH'),
    ).toBe(true);
  },
);
it('sends the chosen display position as a zero-based order', async () => {
  render(<SensorsView />);
  fireEvent.change(await screen.findByLabelText(/Display position/), {
    target: { value: '3' },
  });
  vi.mocked(fetch).mockResolvedValueOnce({
    ok: true,
    json: async () => ({ status: 'queued' }),
  } as Response);
  fireEvent.click(screen.getByRole('button', { name: 'Save sensor settings' }));
  await screen.findByText(
    'Update queued. It applies with the next measurement cycle.',
  );
  const last = vi.mocked(fetch).mock.calls.at(-1)!;
  expect(JSON.parse(last[1]?.body as string)).toMatchObject({ order: 2 });
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

it('disables sensor edits when persistent storage is read-only', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockImplementation(async (input: RequestInfo | URL) => ({
      ok: true,
      json: async () =>
        String(input).endsWith('/sensors/storage')
          ? { schema_version: 1, state: 'read_only', writable: false }
          : [sensor],
    })),
  );
  render(<SensorsView />);
  expect(
    await screen.findByText(
      /stored configuration could not be safely opened or saved/,
    ),
  ).toBeInTheDocument();
  expect(
    screen.getByRole('button', { name: 'Save sensor settings' }),
  ).toBeDisabled();
  expect(screen.getByLabelText(/Sensor name/)).toBeDisabled();
});

it('requires confirmation and queues retirement with the current sensor ETag', async () => {
  vi.spyOn(window, 'confirm').mockReturnValue(true);
  vi.stubGlobal(
    'fetch',
    vi
      .fn()
      .mockImplementation(
        async (input: RequestInfo | URL, init?: RequestInit) => {
          if (init?.method === 'DELETE')
            return {
              ok: true,
              status: 202,
              json: async () => ({ status: 'queued' }),
            };
          return {
            ok: true,
            status: 200,
            json: async () =>
              String(input).endsWith('/sensors/storage')
                ? { schema_version: 1, state: 'ready', writable: true }
                : [{ ...sensor, state: 'disconnected' }],
          };
        },
      ),
  );
  render(<SensorsView />);
  fireEvent.click(
    await screen.findByRole('button', { name: 'Retire disconnected probe' }),
  );
  expect(await screen.findByText(/Retirement queued/)).toBeInTheDocument();
  expect(window.confirm).toHaveBeenCalledWith(
    expect.stringContaining(sensor.rom_id),
  );
  const deletion = vi
    .mocked(fetch)
    .mock.calls.find(([, init]) => init?.method === 'DELETE');
  expect(deletion?.[0]).toBe(`/api/v1/sensors/${sensor.rom_id}`);
  expect(deletion?.[1]?.headers).toMatchObject({ 'If-Match': sensor.etag });
});

it('does not submit retirement when its confirmation is cancelled', async () => {
  vi.spyOn(window, 'confirm').mockReturnValue(false);
  vi.stubGlobal(
    'fetch',
    vi.fn().mockImplementation(async (input: RequestInfo | URL) => ({
      ok: true,
      status: 200,
      json: async () =>
        String(input).endsWith('/sensors/storage')
          ? { schema_version: 1, state: 'ready', writable: true }
          : [{ ...sensor, state: 'disconnected' }],
    })),
  );
  render(<SensorsView />);
  fireEvent.click(
    await screen.findByRole('button', { name: 'Retire disconnected probe' }),
  );
  expect(window.confirm).toHaveBeenCalled();
  expect(
    vi.mocked(fetch).mock.calls.some(([, init]) => init?.method === 'DELETE'),
  ).toBe(false);
});
