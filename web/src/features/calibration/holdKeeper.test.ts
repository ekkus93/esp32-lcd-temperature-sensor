import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { createHoldKeeper } from './holdKeeper';

const MINUTE = 60_000;
beforeEach(() => vi.useFakeTimers());
afterEach(() => vi.useRealTimers());

type Send = (minutes: number) => Promise<void>;
function keeperWith(
  send: ReturnType<typeof vi.fn<Send>> = vi.fn<Send>(async () => undefined),
) {
  const onLost = vi.fn();
  return { keeper: createHoldKeeper({ send, onLost }), send, onLost };
}

it('renews every five minutes from a confirmed start until closed, then sends one tail', async () => {
  const { keeper, send } = keeperWith();
  expect(await keeper.start()).toBe(true);
  expect(keeper.held).toBe(true);
  await vi.advanceTimersByTimeAsync(20 * MINUTE);
  expect(send.mock.calls.map(([m]) => m)).toEqual([15, 15, 15, 15, 15]);
  await keeper.close();
  await keeper.close(); // a second close sends nothing more
  await vi.advanceTimersByTimeAsync(20 * MINUTE);
  expect(send.mock.calls.map(([m]) => m)).toEqual([15, 15, 15, 15, 15, 5]);
  expect(keeper.held).toBe(false);
  expect(vi.getTimerCount()).toBe(0);
});

it('starting again while held keeps a single renewal schedule', async () => {
  const { keeper, send } = keeperWith();
  await keeper.start();
  await keeper.start();
  await vi.advanceTimersByTimeAsync(5 * MINUTE);
  expect(send.mock.calls.length).toBe(3); // two starts and one renewal, not two
  expect(vi.getTimerCount()).toBe(1);
  await keeper.close();
});

it('a refused start rejects with the device error and does not renew', async () => {
  const send = vi.fn<Send>(async () => {
    throw new Error('Probe must be connected to calibrate it');
  });
  const { keeper } = keeperWith(send);
  await expect(keeper.start()).rejects.toThrow('Probe must be connected');
  expect(keeper.held).toBe(false);
  expect(vi.getTimerCount()).toBe(0);
  await keeper.close();
});

it('a failed renewal reports the loss once and stops renewing until started again', async () => {
  const send = vi.fn<Send>(async () => undefined);
  const { keeper, onLost } = keeperWith(send);
  await keeper.start();
  send.mockRejectedValueOnce(new Error('queue full'));
  await vi.advanceTimersByTimeAsync(5 * MINUTE);
  expect(onLost).toHaveBeenCalledOnce();
  expect(keeper.held).toBe(false);
  expect(vi.getTimerCount()).toBe(0);
  expect(await keeper.start()).toBe(true);
  expect(vi.getTimerCount()).toBe(1);
  await keeper.close();
});

it('requests go out strictly in order, so the tail cannot be overtaken', async () => {
  const order: string[] = [];
  const answers: Array<() => void> = [];
  const send = vi.fn<Send>(
    (minutes: number) =>
      new Promise<void>((resolve) => {
        order.push(`sent ${minutes}`);
        answers.push(() => {
          order.push(`answered ${minutes}`);
          resolve();
        });
      }),
  );
  const { keeper } = keeperWith(send);
  const started = keeper.start();
  const closed = keeper.close();
  await Promise.resolve();
  expect(order).toEqual(['sent 15']); // the tail waits
  answers[0]();
  await started;
  await vi.waitFor(() =>
    expect(order).toEqual(['sent 15', 'answered 15', 'sent 5']),
  );
  answers[1]();
  await closed;
  expect(vi.getTimerCount()).toBe(0); // the late start did not install renewal
});

it('closing a keeper that never started sends no tail', async () => {
  const { keeper, send } = keeperWith();
  await keeper.close();
  expect(send).not.toHaveBeenCalled();
});
