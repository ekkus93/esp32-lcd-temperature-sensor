import { expect, it, vi } from 'vitest';
import { reloadOnStaleBuild } from './staleBuild';

function setup(start = 1_000_000) {
  const target = new EventTarget();
  const store = new Map<string, string>();
  const storage = {
    getItem: (key: string) => store.get(key) ?? null,
    setItem: (key: string, value: string) => void store.set(key, value),
  };
  let time = start;
  const reload = vi.fn();
  reloadOnStaleBuild(target as unknown as Window, storage, reload, () => time);
  const fail = () => {
    const event = new Event('vite:preloadError', { cancelable: true });
    target.dispatchEvent(event);
    return event.defaultPrevented;
  };
  return { reload, fail, advance: (ms: number) => (time += ms) };
}

it('reloads once when page code from an older build cannot be loaded', () => {
  const { reload, fail } = setup();
  expect(fail()).toBe(true);
  expect(reload).toHaveBeenCalledTimes(1);
});

it('does not reload again within a minute, so an outage cannot loop', () => {
  const { reload, fail, advance } = setup();
  fail();
  advance(30000);
  expect(fail()).toBe(false); // left to the error screen
  expect(reload).toHaveBeenCalledTimes(1);
  advance(31000);
  fail();
  expect(reload).toHaveBeenCalledTimes(2);
});
