import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { delayedError } from './delayedError';

beforeEach(() => vi.useFakeTimers());
afterEach(() => vi.useRealTimers());

it('shows a problem only after it has lasted the delay', () => {
  const show = vi.fn();
  const problem = delayedError(show, 10000);
  problem.fail('Live connection lost');
  vi.advanceTimersByTime(9999);
  expect(show).not.toHaveBeenCalled();
  vi.advanceTimersByTime(1);
  expect(show).toHaveBeenCalledWith('Live connection lost');
});

it('never shows a problem that recovers within the delay', () => {
  const show = vi.fn();
  const problem = delayedError(show, 10000);
  problem.fail('Live connection lost');
  vi.advanceTimersByTime(5000);
  problem.recover();
  vi.advanceTimersByTime(20000);
  expect(show).not.toHaveBeenCalledWith('Live connection lost');
  expect(show).toHaveBeenLastCalledWith('');
});

it('still reports a later problem after a recovery', () => {
  const show = vi.fn();
  const problem = delayedError(show, 10000);
  problem.fail('first');
  problem.recover();
  problem.fail('second');
  vi.advanceTimersByTime(10000);
  expect(show).toHaveBeenLastCalledWith('second');
});

it('shows the message that started the problem', () => {
  const show = vi.fn();
  const problem = delayedError(show, 10000);
  problem.fail('first');
  vi.advanceTimersByTime(4000);
  problem.fail('second');
  vi.advanceTimersByTime(6000);
  expect(show).toHaveBeenCalledTimes(1);
  expect(show).toHaveBeenCalledWith('first');
});

it('shows nothing after it is cancelled', () => {
  const show = vi.fn();
  const problem = delayedError(show, 10000);
  problem.fail('Live connection lost');
  problem.cancel();
  vi.advanceTimersByTime(20000);
  expect(show).not.toHaveBeenCalled();
});
