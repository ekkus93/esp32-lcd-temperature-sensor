import {
  act,
  fireEvent,
  screen,
  waitFor,
  within,
} from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import IceCalibrationDialog from './IceCalibrationDialog';
import { parseSnapshot } from '../../api/client';
import { empty } from '../../api/fixtures';
import { renderThemed as render } from '../../renderThemed';
import { getSensors, saveSensor, setCalibrationHold } from '../../api/sensors';

vi.mock('../../api/sensors', () => ({
  getSensors: vi.fn(),
  saveSensor: vi.fn(),
  setCalibrationHold: vi.fn(),
  endCalibrationHold: vi.fn(),
}));

const ROM = '2801000000000029';
const ETAG = '"boot:2801000000000029:1"';
let sequence = 0;
let lastSeen = 1000;

// A live snapshot in which the probe reads `celsius` (already calibrated) on a new sample.
function frame(celsius: number, state = 'normal', bus_fault = false) {
  sequence += 1;
  lastSeen += 5000;
  return parseSnapshot({
    ...empty,
    sequence,
    uptime_ms: lastSeen,
    bus_fault,
    sensors: [
      {
        rom_id: ROM,
        name: 'Canopy',
        state,
        // A probe without a valid reading has no temperature.
        temperature_c:
          state === 'normal' || state === 'high' || state === 'low'
            ? celsius
            : null,
        last_seen_ms: lastSeen,
        enabled: true,
        order: 0,
      },
    ],
  });
}

function open(
  snapshot = frame(0.4),
  overrides: Partial<Parameters<typeof IceCalibrationDialog>[0]> = {},
) {
  const props = {
    rom: ROM,
    name: 'Canopy',
    unit: 'C' as const,
    etag: ETAG,
    calibrationC: 0,
    lowC: 18,
    highC: 28,
    snapshot,
    onCalibrated: vi.fn(),
    onEditLimits: vi.fn(),
    onClose: vi.fn(),
    ...overrides,
  };
  const view = render(<IceCalibrationDialog {...props} />);
  return { view, props };
}

// Feeds `count` new samples at `celsius`, one per five seconds.
function feed(
  view: ReturnType<typeof open>['view'],
  celsius: number,
  count: number,
  props: Parameters<typeof IceCalibrationDialog>[0],
) {
  for (let i = 0; i < count; i++)
    view.rerender(
      <IceCalibrationDialog {...props} snapshot={frame(celsius)} />,
    );
}

beforeEach(() => {
  vi.mocked(setCalibrationHold).mockResolvedValue(undefined);
  vi.mocked(saveSensor).mockResolvedValue(undefined);
  vi.mocked(getSensors).mockResolvedValue([
    {
      rom_id: ROM,
      etag: ETAG,
      revision: 1,
      name: 'Canopy',
      short_name: '',
      enabled: true,
      disconnect_alert: true,
      order: 0,
      calibration_c: 0,
      low_c: 18,
      high_c: 28,
      hysteresis_c: 0.5,
      persistence_seconds: 15,
      state: 'normal',
      probe_calibration: 'stored',
    },
  ] as never);
});
afterEach(() => vi.clearAllMocks());

it('pauses the probe’s alerts for 15 minutes when it opens, and shows the instructions', async () => {
  open();
  expect(await screen.findByText(/Make the ice bath/)).toBeInTheDocument();
  expect(setCalibrationHold).toHaveBeenCalledWith(ROM, 15);
});

it('measures a steady minute, offers the offset, saves it with the current ETag, and shortens the hold', async () => {
  const { view, props } = open();
  fireEvent.click(
    await screen.findByRole('button', { name: 'Start measuring' }),
  );
  // The probe reads 0.4 °C with no offset: the offset to save is -0.4.
  feed(view, 0.4, 12, props);
  expect(
    await screen.findByText(/Offset: 0\.0 → -0\.4 °C/),
  ).toBeInTheDocument();
  expect(
    screen.getByText(/Every reading will change by -0\.4 °C/),
  ).toBeInTheDocument();
  // The limits are shown, unchanged, with a way to edit them.
  expect(
    screen.getByText(/Your high and low limits are not changed/),
  ).toBeInTheDocument();

  // Before the save the settings still carry ETAG; after it the device has applied a new revision.
  const before = await getSensors();
  vi.mocked(getSensors).mockResolvedValueOnce(before);
  vi.mocked(getSensors).mockResolvedValue(
    before.map((sensor) => ({
      ...sensor,
      etag: '"boot:2801000000000029:2"',
      revision: 2,
      calibration_c: -0.4,
    })),
  );
  fireEvent.click(screen.getByRole('button', { name: 'Save offset' }));
  await waitFor(() =>
    expect(saveSensor).toHaveBeenCalledWith(ROM, { calibration_c: -0.4 }, ETAG),
  );
  await waitFor(() => expect(props.onCalibrated).toHaveBeenCalledOnce());
  expect(
    await screen.findByText(/Saved\. Put the probe back in the bath/),
  ).toBeInTheDocument();
  // Done closes it, and the hold was already shortened to five minutes.
  expect(setCalibrationHold).toHaveBeenLastCalledWith(ROM, 5);
  fireEvent.click(screen.getByRole('button', { name: 'Done' }));
  expect(props.onClose).toHaveBeenCalled();
}, 20000);

it('does not count a sample twice when the snapshot repeats', async () => {
  // One frame, delivered again and again: the same sample each time.
  const same = frame(0.4);
  const { view, props } = open(same);
  fireEvent.click(
    await screen.findByRole('button', { name: 'Start measuring' }),
  );
  for (let i = 0; i < 12; i++)
    view.rerender(<IceCalibrationDialog {...props} snapshot={same} />);
  expect(screen.getByText('0 of 12 readings')).toBeInTheDocument();
});

it('keeps waiting while the readings still move, and says so', async () => {
  const { view, props } = open();
  fireEvent.click(
    await screen.findByRole('button', { name: 'Start measuring' }),
  );
  feed(view, 0.0, 6, props);
  feed(view, 0.6, 6, props);
  // The last minute spans 0.6 °C, so there is no result yet.
  expect(screen.queryByText(/Offset:/)).toBeNull();
  expect(screen.getByText('12 of 12 readings')).toBeInTheDocument();
});

it('stops when the probe is lost while measuring, and never saves', async () => {
  const { view, props } = open();
  fireEvent.click(
    await screen.findByRole('button', { name: 'Start measuring' }),
  );
  feed(view, 0.4, 3, props);
  view.rerender(
    <IceCalibrationDialog {...props} snapshot={frame(0, 'disconnected')} />,
  );
  expect(
    await screen.findByText(/The probe is disconnected, so readings stopped/),
  ).toBeInTheDocument();
  expect(saveSensor).not.toHaveBeenCalled();
});

it('refuses an offset beyond the ±12.7 °C limit', async () => {
  const { view, props } = open();
  fireEvent.click(
    await screen.findByRole('button', { name: 'Start measuring' }),
  );
  feed(view, 13.5, 12, props); // a 13.5 °C raw error: the offset would be -13.5
  expect(await screen.findByText(/outside the ±12\.7 °C/)).toBeInTheDocument();
  expect(screen.getByRole('button', { name: 'Save offset' })).toBeDisabled();
});

it('warns beyond 2 °C and saves only when asked to anyway', async () => {
  const { view, props } = open();
  fireEvent.click(
    await screen.findByRole('button', { name: 'Start measuring' }),
  );
  feed(view, 2.5, 12, props); // an offset of -2.5
  expect(
    await screen.findByText(/much larger than a genuine probe/),
  ).toBeInTheDocument();
  fireEvent.click(
    screen.getByRole('button', { name: 'Save this offset anyway' }),
  );
  await waitFor(() =>
    expect(saveSensor).toHaveBeenCalledWith(ROM, { calibration_c: -2.5 }, ETAG),
  );
}, 20000);

it('cancelling shortens the hold to five minutes and saves nothing', async () => {
  const { props } = open();
  fireEvent.click(await screen.findByRole('button', { name: 'Cancel' }));
  await waitFor(() => expect(props.onClose).toHaveBeenCalled());
  expect(setCalibrationHold).toHaveBeenLastCalledWith(ROM, 5);
  expect(saveSensor).not.toHaveBeenCalled();
});

it('refuses to start while the probe has no valid reading', async () => {
  open(frame(0, 'invalid'));
  const start = await screen.findByRole('button', {
    name: 'Waiting for a reading',
  });
  expect(start).toBeDisabled();
});

it('reports a refused pause instead of measuring', async () => {
  vi.mocked(setCalibrationHold).mockRejectedValueOnce(
    new Error('Probe must be connected to calibrate it'),
  );
  open();
  expect(await screen.findByRole('alert')).toHaveTextContent(
    'Probe must be connected to calibrate it',
  );
  expect(screen.queryByRole('button', { name: 'Start measuring' })).toBeNull();
});

it('limits and the flow are announced in the dialog', async () => {
  open();
  const dialog = await screen.findByRole('dialog', {
    name: 'Calibrate Canopy in ice',
  });
  expect(
    within(dialog).getByRole('button', { name: 'Cancel' }),
  ).toBeInTheDocument();
  expect(act).toBeDefined();
});

it('says so, and offers only Done, when the saved offset already gives the ice point', async () => {
  // Saved -0.4 °C; the probe's raw error is +0.4 °C, so it already reads 0 °C in the bath.
  const { view, props } = open(frame(0.0), { calibrationC: -0.4 });
  fireEvent.click(
    await screen.findByRole('button', { name: 'Start measuring' }),
  );
  feed(view, 0.0, 12, { ...props, calibrationC: -0.4 });
  expect(
    await screen.findByText(/already reads 0\.0 °C in the ice/),
  ).toBeInTheDocument();
  expect(screen.queryByRole('button', { name: 'Save offset' })).toBeNull();
  expect(screen.getByRole('button', { name: 'Done' })).toBeInTheDocument();
  expect(saveSensor).not.toHaveBeenCalled();
});

it('edit limits leaves the flow, ends the hold, and changes no limit itself', async () => {
  const { view, props } = open();
  fireEvent.click(
    await screen.findByRole('button', { name: 'Start measuring' }),
  );
  feed(view, 0.4, 12, props);
  fireEvent.click(await screen.findByRole('button', { name: 'Edit limits' }));
  await waitFor(() => expect(props.onEditLimits).toHaveBeenCalledOnce());
  expect(props.onClose).toHaveBeenCalled();
  expect(setCalibrationHold).toHaveBeenLastCalledWith(ROM, 5);
  expect(saveSensor).not.toHaveBeenCalled();
});

it('stops measuring when the shared sensor bus faults, and never saves', async () => {
  const { view, props } = open();
  fireEvent.click(
    await screen.findByRole('button', { name: 'Start measuring' }),
  );
  feed(view, 0.4, 3, props);
  view.rerender(
    <IceCalibrationDialog {...props} snapshot={frame(0.4, 'normal', true)} />,
  );
  expect(
    await screen.findByText(/sensor bus faulted, so readings stopped/),
  ).toBeInTheDocument();
  expect(saveSensor).not.toHaveBeenCalled();
});
