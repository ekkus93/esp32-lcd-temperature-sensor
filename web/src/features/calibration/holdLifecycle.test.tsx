// F1 (review audit A01): the alert pause must be renewed for as long as the flow is open, whatever
// step it is on, and end with one five-minute tail.
import { act, fireEvent, screen } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import IceCalibrationDialog, {
  type IceCalibrationProps,
} from './IceCalibrationDialog';
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
const MINUTE = 60_000;
let lastSeen = 1000;
function frame(celsius: number) {
  lastSeen += 5000;
  return parseSnapshot({
    ...empty,
    sequence: lastSeen,
    uptime_ms: lastSeen,
    sensors: [
      {
        rom_id: ROM,
        name: 'Canopy',
        state: 'normal',
        temperature_c: celsius,
        last_seen_ms: lastSeen,
        enabled: true,
        order: 0,
      },
    ],
  });
}
function props(snapshot = frame(0.4)): IceCalibrationProps {
  return {
    rom: ROM,
    name: 'Canopy',
    unit: 'C',
    etag: ETAG,
    calibrationC: 0,
    lowC: 18,
    highC: 28,
    snapshot,
    onCalibrated: vi.fn(),
    onEditLimits: vi.fn(),
    onClose: vi.fn(),
  };
}
const holdCalls = () =>
  vi.mocked(setCalibrationHold).mock.calls.map(([, minutes]) => minutes);
// Lets queued promise callbacks run without moving the clock.
const flush = () =>
  act(async () => {
    await Promise.resolve();
    await Promise.resolve();
  });
async function advance(ms: number) {
  await act(async () => {
    await vi.advanceTimersByTimeAsync(ms);
  });
}

beforeEach(() => {
  vi.useFakeTimers();
  vi.mocked(setCalibrationHold).mockResolvedValue(undefined);
  vi.mocked(saveSensor).mockResolvedValue(undefined);
  vi.mocked(getSensors).mockResolvedValue([
    { rom_id: ROM, etag: ETAG },
  ] as never);
});
afterEach(() => {
  vi.useRealTimers();
  vi.clearAllMocks();
});

it('keeps renewing the pause while a steady result stays open past twenty minutes', async () => {
  const p = props();
  const view = render(<IceCalibrationDialog {...p} />);
  await flush();
  fireEvent.click(screen.getByRole('button', { name: 'Start measuring' }));
  for (let i = 0; i < 12; i++) {
    view.rerender(<IceCalibrationDialog {...p} snapshot={frame(0.4)} />);
    await advance(5000);
  }
  expect(screen.getByText(/Offset:/)).toBeInTheDocument();
  const before = holdCalls().length;
  await advance(21 * MINUTE);
  // One renewal every five minutes while the result is shown: at least four in 21 minutes.
  expect(holdCalls().length - before).toBeGreaterThanOrEqual(4);
  expect(
    holdCalls()
      .slice(before)
      .every((minutes) => minutes === 15),
  ).toBe(true);
});

it('keeps renewing through a ten-minute give-up and a retry', async () => {
  const p = props();
  const view = render(<IceCalibrationDialog {...p} />);
  await flush();
  fireEvent.click(screen.getByRole('button', { name: 'Start measuring' }));
  await advance(10 * MINUTE + 1000);
  expect(
    screen.getByText(/No steady reading in 10 minutes/),
  ).toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Start again' }));
  fireEvent.click(screen.getByRole('button', { name: 'Start measuring' }));
  const before = holdCalls().length;
  await advance(16 * MINUTE);
  expect(holdCalls().length - before).toBeGreaterThanOrEqual(3);
  view.unmount();
});

it('stops claiming the pause when a renewal fails, and pauses again before measuring', async () => {
  const p = props();
  render(<IceCalibrationDialog {...p} />);
  await flush();
  fireEvent.click(screen.getByRole('button', { name: 'Start measuring' }));
  vi.mocked(setCalibrationHold).mockRejectedValueOnce(
    new Error('Configuration queue full'),
  );
  await advance(5 * MINUTE);
  expect(screen.getByRole('alert')).toHaveTextContent(
    /no longer paused: the device did not renew the pause \(Configuration queue full\)/,
  );
  // Measuring stopped; nothing can be saved while the pause is not assured.
  expect(screen.queryByRole('button', { name: 'Save offset' })).toBeNull();
  expect(screen.queryByText(/of 12 readings/)).toBeNull();
  fireEvent.click(screen.getByRole('button', { name: 'Pause alerts again' }));
  await flush();
  expect(
    screen.getByRole('button', { name: 'Start measuring' }),
  ).toBeInTheDocument();
  // Renewal is running again.
  const before = holdCalls().length;
  await advance(5 * MINUTE);
  expect(holdCalls().length - before).toBe(1);
});

it('a first answer that arrives after the page closed starts no renewal, and the tail goes last', async () => {
  let answer: () => void = () => {};
  vi.mocked(setCalibrationHold).mockImplementationOnce(
    () => new Promise<void>((resolve) => (answer = resolve)),
  );
  const view = render(<IceCalibrationDialog {...props()} />);
  await flush(); // the first request is sent and not yet answered
  view.unmount();
  await flush();
  expect(holdCalls()).toEqual([15]); // the tail waits for the first request
  answer();
  await flush();
  await advance(20 * MINUTE);
  expect(holdCalls()).toEqual([15, 5]);
});

it('a renewal still in flight when Done is pressed is followed by the tail, and nothing after', async () => {
  const p = props();
  render(<IceCalibrationDialog {...p} />);
  await flush();
  let answer: () => void = () => {};
  vi.mocked(setCalibrationHold).mockImplementationOnce(
    () => new Promise<void>((resolve) => (answer = resolve)),
  );
  await advance(5 * MINUTE); // the renewal is sent and not yet answered
  fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
  await flush();
  expect(holdCalls()).toEqual([15, 15]);
  answer();
  await flush();
  await advance(20 * MINUTE);
  expect(holdCalls()).toEqual([15, 15, 5]);
  expect(p.onClose).toHaveBeenCalledOnce();
});

it.each([
  [
    'Cancel',
    (view: ReturnType<typeof render>) =>
      fireEvent.click(screen.getByRole('button', { name: 'Cancel' })) && view,
  ],
  [
    'Escape',
    () => fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' }),
  ],
  ['leaving the page', (view: ReturnType<typeof render>) => view.unmount()],
])(
  '%s sends exactly one five-minute tail and stops renewing',
  async (_name, leave) => {
    const view = render(<IceCalibrationDialog {...props()} />);
    await flush();
    leave(view);
    await flush();
    view.unmount();
    await flush();
    await advance(20 * MINUTE);
    expect(holdCalls()).toEqual([15, 5]);
  },
);

it('a failed tail raises no error and leaves no timer behind', async () => {
  const p = props();
  render(<IceCalibrationDialog {...p} />);
  await flush();
  vi.mocked(setCalibrationHold).mockRejectedValueOnce(
    new Error('Device unreachable'),
  );
  fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
  await flush();
  expect(p.onClose).toHaveBeenCalledOnce();
  // No renewal is left running (the dialog's own exit animation timers are not ours). An unhandled
  // rejection from the failed tail would fail this test run on its own.
  await advance(20 * MINUTE);
  expect(holdCalls()).toEqual([15, 5]);
});

it('closing before any pause was requested sends nothing', async () => {
  const view = render(<IceCalibrationDialog {...props()} />);
  view.unmount(); // before the first request went out
  await flush();
  await advance(20 * MINUTE);
  expect(holdCalls()).toEqual([]);
});
