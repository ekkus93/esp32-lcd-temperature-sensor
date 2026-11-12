import { fireEvent, render, screen, within } from '@testing-library/react';
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
  probe_calibration: 'stored',
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
  render(<SensorsView unit="C" />);
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
  // The probe memory status is read-only; the device rejects it in a PATCH.
  expect(JSON.parse(last[1]?.body as string)).not.toHaveProperty(
    'probe_calibration',
  );
});
it.each([
  ['stored', 'C', /Also saved on the probe itself/],
  ['restored', 'C', /Read from the probe's own memory/],
  ['pending', 'C', /Saving a copy on the probe/],
  ['failed', 'C', /Could not be saved on the probe/],
  ['unknown', 'C', /Not read from the probe yet/],
] as const)(
  'explains probe memory state %s under the calibration offset (%s)',
  async (probe_calibration, unit, text) => {
    stubSensors([{ ...sensor, probe_calibration }]);
    render(<SensorsView unit={unit} />);
    expect(
      await screen.findByLabelText(/Calibration offset/),
    ).toHaveAccessibleDescription(text);
  },
);
const limited = { ...sensor, low_c: 2, high_c: 8, calibration_c: 0.5 };
function stubLimited() {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockImplementation(async (input: RequestInfo | URL) => ({
      ok: true,
      json: async () =>
        String(input).endsWith('/sensors/storage')
          ? { schema_version: 1, state: 'ready', writable: true }
          : [limited],
    })),
  );
}
it('shows thresholds and calibration in Fahrenheit and stores Celsius', async () => {
  stubLimited();
  render(<SensorsView unit="F" />);
  // Absolute temperatures convert with the 32 degree offset; differences do not.
  expect(await screen.findByLabelText(/Low threshold/)).toHaveValue(35.6);
  expect(screen.getByLabelText(/Low threshold \(°F\)/)).toBeInTheDocument();
  expect(screen.getByLabelText(/High threshold/)).toHaveValue(46.4);
  expect(screen.getByLabelText(/Calibration offset \(°F\)/)).toHaveValue(0.9);
  expect(screen.getByLabelText(/Hysteresis \(°F\)/)).toHaveValue(0.9);
  fireEvent.change(screen.getByLabelText(/High threshold/), {
    target: { value: '50' },
  });
  fireEvent.change(screen.getByLabelText(/Calibration offset/), {
    target: { value: '1.8' },
  });
  vi.mocked(fetch).mockResolvedValueOnce({
    ok: true,
    json: async () => ({ status: 'queued' }),
  } as Response);
  fireEvent.click(screen.getByRole('button', { name: 'Save sensor settings' }));
  await screen.findByText(
    'Update queued. It applies with the next measurement cycle.',
  );
  const body = JSON.parse(
    vi.mocked(fetch).mock.calls.at(-1)![1]?.body as string,
  );
  expect(body.high_c).toBeCloseTo(10, 6); // 50 °F
  expect(body.calibration_c).toBeCloseTo(1, 6); // a 1.8 °F difference is 1 °C
  // Fields the user did not touch are sent exactly as stored, with no conversion round trip.
  expect(body.low_c).toBe(2);
  expect(body.hysteresis_c).toBe(0.5);
});
it('keeps Celsius entry exact when the configured unit is Celsius', async () => {
  stubLimited();
  render(<SensorsView unit="C" />);
  expect(await screen.findByLabelText(/Low threshold \(°C\)/)).toHaveValue(2);
  expect(screen.getByLabelText(/Calibration offset \(°C\)/)).toHaveValue(0.5);
});
it('rejects reversed thresholds entered in Fahrenheit', async () => {
  stubLimited();
  render(<SensorsView unit="F" />);
  fireEvent.change(await screen.findByLabelText(/Low threshold/), {
    target: { value: '60' },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Save sensor settings' }));
  await screen.findByText('Low threshold must be below high threshold.');
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
  render(<SensorsView unit="C" />);
  expect(
    await screen.findByText(
      'No sensors discovered. Unused positions do not cause alarms.',
    ),
  ).toBeInTheDocument();
});
function stubSensors(list: object[]) {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockImplementation(async (input: RequestInfo | URL) => ({
      ok: true,
      json: async () =>
        String(input).endsWith('/sensors/storage')
          ? { schema_version: 1, state: 'ready', writable: true }
          : list,
    })),
  );
}
it('gives each of five sensors a tab with its maximum-length name', async () => {
  const sensors = Array.from({ length: 5 }, (_, index) => ({
    ...sensor,
    rom_id: `28${String(index + 1).padStart(14, '0')}`,
    etag: `"simulator:${index}:1"`,
    name: `${index + 1}${'x'.repeat(31)}`,
    order: index,
  }));
  stubSensors(sensors);
  render(<SensorsView unit="C" />);
  const tabs = await screen.findAllByRole('tab');
  expect(tabs.map((tab) => tab.textContent)).toEqual(
    sensors.map((item) => item.name),
  );
  expect(tabs[0]).toHaveAttribute('aria-selected', 'true');
  // Only the chosen sensor's editor is shown.
  expect(screen.getAllByRole('tabpanel')).toHaveLength(1);
  expect(screen.getByRole('tabpanel')).toHaveAccessibleName(sensors[0].name);
});
it('orders the tabs by LCD display position and switches editors', async () => {
  stubSensors([
    { ...sensor, rom_id: '2800000000000002', name: 'Freezer', order: 1 },
    { ...sensor, rom_id: '2800000000000001', name: 'Fridge', order: 0 },
  ]);
  render(<SensorsView unit="C" />);
  const tabs = await screen.findAllByRole('tab');
  expect(tabs.map((tab) => tab.textContent)).toEqual(['Fridge', 'Freezer']);
  expect(screen.getByRole('tabpanel')).toHaveTextContent('2800000000000001');
  fireEvent.click(screen.getByRole('tab', { name: 'Freezer' }));
  expect(screen.getByRole('tab', { name: 'Freezer' })).toHaveAttribute(
    'aria-selected',
    'true',
  );
  expect(screen.getByRole('tabpanel')).toHaveTextContent('2800000000000002');
});
it('keeps unsaved edits when switching to another sensor and back', async () => {
  stubSensors([
    { ...sensor, rom_id: '2800000000000001', name: 'Fridge', order: 0 },
    { ...sensor, rom_id: '2800000000000002', name: 'Freezer', order: 1 },
  ]);
  render(<SensorsView unit="C" />);
  await screen.findAllByRole('tab');
  const panel = () => within(screen.getByRole('tabpanel'));
  fireEvent.change(panel().getByLabelText(/High threshold/), {
    target: { value: '7' },
  });
  fireEvent.click(screen.getByRole('tab', { name: 'Freezer' }));
  expect(panel().getByLabelText(/High threshold/)).toHaveValue(null);
  fireEvent.click(screen.getByRole('tab', { name: 'Fridge' }));
  expect(panel().getByLabelText(/High threshold/)).toHaveValue(7);
});
it('renames the tab and keeps the first change when a second save follows', async () => {
  let stored: typeof sensor & { high_c: number | null } = { ...sensor };
  const patches: Record<string, unknown>[] = [];
  vi.stubGlobal(
    'fetch',
    vi
      .fn()
      .mockImplementation(
        async (input: RequestInfo | URL, init?: RequestInit) => {
          if (init?.method === 'PATCH') {
            const patch = JSON.parse(init.body as string);
            patches.push(patch);
            // The device applies the patch and moves to a new revision.
            const revision = stored.revision + 1;
            stored = {
              ...stored,
              ...patch,
              revision,
              etag: `"simulator:${sensor.rom_id}:${revision}"`,
            };
            return { ok: true, json: async () => ({ status: 'queued' }) };
          }
          return {
            ok: true,
            json: async () =>
              String(input).endsWith('/sensors/storage')
                ? { schema_version: 1, state: 'ready', writable: true }
                : [stored],
          };
        },
      ),
  );
  render(<SensorsView unit="C" />);
  fireEvent.change(await screen.findByLabelText(/High threshold/), {
    target: { value: '8' },
  });
  fireEvent.change(screen.getByLabelText(/Sensor name/), {
    target: { value: 'Fridge' },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Save sensor settings' }));
  await screen.findByText('Settings committed and applied.', undefined, {
    timeout: 3000,
  });
  expect(screen.getByRole('tab', { name: 'Fridge' })).toBeInTheDocument();
  fireEvent.change(screen.getByLabelText(/Low threshold/), {
    target: { value: '2' },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Save sensor settings' }));
  await screen.findByText(
    'Update queued. It applies with the next measurement cycle.',
  );
  // The second save must not send the high threshold as it was before the first save.
  expect(patches[1]).toMatchObject({ low_c: 2, high_c: 8, name: 'Fridge' });
});
it('rejects reversed thresholds before sending an update', async () => {
  render(<SensorsView unit="C" />);
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
    render(<SensorsView unit="C" />);
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
    render(<SensorsView unit="C" />);
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
  render(<SensorsView unit="C" />);
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
  render(<SensorsView unit="C" />);
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
  render(<SensorsView unit="C" />);
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
  render(<SensorsView unit="C" />);
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
  render(<SensorsView unit="C" />);
  fireEvent.click(
    await screen.findByRole('button', { name: 'Retire disconnected probe' }),
  );
  expect(window.confirm).toHaveBeenCalled();
  expect(
    vi.mocked(fetch).mock.calls.some(([, init]) => init?.method === 'DELETE'),
  ).toBe(false);
});

function stubSaving() {
  const patches: Record<string, unknown>[] = [];
  vi.stubGlobal(
    'fetch',
    vi
      .fn()
      .mockImplementation(
        async (input: RequestInfo | URL, init?: RequestInit) => {
          if (init?.method === 'PATCH') {
            patches.push(JSON.parse(init.body as string));
            return { ok: true, json: async () => ({ status: 'queued' }) };
          }
          return {
            ok: true,
            json: async () =>
              String(input).endsWith('/sensors/storage')
                ? { schema_version: 1, state: 'ready', writable: true }
                : [limited],
          };
        },
      ),
  );
  return patches;
}
const save = async () => {
  fireEvent.click(screen.getByRole('button', { name: 'Save sensor settings' }));
  await screen.findByText(
    'Update queued. It applies with the next measurement cycle.',
  );
};
it('starts in the display unit and switches entry between °C and °F', async () => {
  stubLimited();
  render(<SensorsView unit="C" />);
  expect(await screen.findByLabelText(/Low threshold \(°C\)/)).toHaveValue(2);
  expect(screen.getByRole('button', { name: 'Celsius' })).toHaveAttribute(
    'aria-pressed',
    'true',
  );
  fireEvent.click(screen.getByRole('button', { name: 'Fahrenheit' }));
  expect(screen.getByLabelText(/Low threshold \(°F\)/)).toHaveValue(35.6);
  expect(screen.getByLabelText(/High threshold \(°F\)/)).toHaveValue(46.4);
  expect(screen.getByLabelText(/Calibration offset \(°F\)/)).toHaveValue(0.9);
  expect(screen.getByLabelText(/Hysteresis \(°F\)/)).toHaveValue(0.9);
  fireEvent.click(screen.getByRole('button', { name: 'Celsius' }));
  expect(screen.getByLabelText(/Low threshold \(°C\)/)).toHaveValue(2);
  expect(screen.getByLabelText(/Calibration offset \(°C\)/)).toHaveValue(0.5);
});
it('converts typed values when the unit changes and still saves Celsius', async () => {
  const patches = stubSaving();
  render(<SensorsView unit="F" />);
  fireEvent.change(await screen.findByLabelText(/High threshold/), {
    target: { value: '50' },
  });
  fireEvent.change(screen.getByLabelText(/Calibration offset/), {
    target: { value: '0.22' },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Celsius' }));
  // 50 °F is 10 °C; a 0.22 °F difference is 0.12 °C to the nearest 0.01.
  expect(screen.getByLabelText(/High threshold \(°C\)/)).toHaveValue(10);
  expect(screen.getByLabelText(/Calibration offset \(°C\)/)).toHaveValue(0.12);
  fireEvent.click(screen.getByRole('button', { name: 'Fahrenheit' }));
  expect(screen.getByLabelText(/High threshold \(°F\)/)).toHaveValue(50);
  expect(screen.getByLabelText(/Calibration offset \(°F\)/)).toHaveValue(0.22);
  fireEvent.click(screen.getByRole('button', { name: 'Celsius' }));
  await save();
  expect(patches[0]).toMatchObject({ high_c: 10, calibration_c: 0.12 });
  // Untouched fields are sent exactly as stored, whatever the unit switches did.
  expect(patches[0]).toMatchObject({ low_c: 2, hysteresis_c: 0.5 });
});
it('saves the °F calibration limit as exactly 12.7 °C', async () => {
  const patches = stubSaving();
  render(<SensorsView unit="F" />);
  const field = await screen.findByLabelText(/Calibration offset \(°F\)/);
  expect(field).toHaveAttribute('max', '22.86');
  expect(field).toHaveAttribute('min', '-22.86');
  fireEvent.change(field, { target: { value: '-22.86' } });
  await save();
  expect(patches[0].calibration_c).toBe(-12.7);
});
it('limits the calibration offset to what the probe can hold', async () => {
  render(<SensorsView unit="C" />);
  const field = await screen.findByLabelText(/Calibration offset \(°C\)/);
  expect(field).toHaveAttribute('max', '12.7');
  expect(field).toHaveAttribute('min', '-12.7');
});
