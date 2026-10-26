import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { subscribeLive } from './live';
import { empty } from './fixtures';
class FakeSocket {
  static instances: FakeSocket[] = [];
  onopen: (() => void) | null = null;
  onmessage: ((e: { data: unknown }) => void) | null = null;
  onclose: (() => void) | null = null;
  onerror: (() => void) | null = null;
  constructor() {
    FakeSocket.instances.push(this);
  }
  close() {
    this.onclose?.();
  }
}
let stop: (() => void) | undefined;
beforeEach(() => {
  vi.useFakeTimers();
  FakeSocket.instances = [];
  vi.stubGlobal('WebSocket', FakeSocket);
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => empty }),
  );
});
afterEach(() => {
  stop?.();
  vi.useRealTimers();
  vi.unstubAllGlobals();
});
it('resynchronizes on gaps and malformed messages', async () => {
  const receive = vi.fn();
  const error = vi.fn();
  stop = subscribeLive(receive, error);
  await vi.advanceTimersByTimeAsync(1);
  const socket = FakeSocket.instances[0];
  socket.onmessage?.({ data: JSON.stringify({ ...empty, sequence: 8 }) });
  await vi.advanceTimersByTimeAsync(1);
  expect(fetch).toHaveBeenCalledTimes(2);
  socket.onmessage?.({ data: '{bad json' });
  await vi.advanceTimersByTimeAsync(1);
  expect(error).toHaveBeenCalledWith('Invalid live update; resynchronizing');
  expect(fetch).toHaveBeenCalledTimes(3);
});
it('reconnects after close and stops all retries when unsubscribed', async () => {
  stop = subscribeLive(vi.fn(), vi.fn());
  FakeSocket.instances[0].onclose?.();
  await vi.advanceTimersByTimeAsync(1000);
  expect(FakeSocket.instances).toHaveLength(2);
  stop();
  await vi.advanceTimersByTimeAsync(60000);
  expect(FakeSocket.instances).toHaveLength(2);
});
it('marks silent connections stale', async () => {
  const error = vi.fn();
  stop = subscribeLive(vi.fn(), error);
  await vi.advanceTimersByTimeAsync(16000);
  expect(error).toHaveBeenCalledWith(
    'Live connection timed out; readings are stale',
  );
});
it('does not let a delayed initial REST response replace a newer boot from WebSocket', async () => {
  let finish!: (value: unknown) => void;
  vi.mocked(fetch).mockImplementationOnce(
    () =>
      new Promise((resolve) => {
        finish = resolve;
      }) as Promise<Response>,
  );
  const receive = vi.fn();
  stop = subscribeLive(receive, vi.fn());
  FakeSocket.instances[0].onmessage?.({
    data: JSON.stringify({ ...empty, boot_id: 'new-boot' }),
  });
  finish({
    ok: true,
    json: async () => ({ ...empty, boot_id: 'old-boot', sequence: 999 }),
  });
  await vi.advanceTimersByTimeAsync(1);
  expect(receive).toHaveBeenCalledOnce();
  expect(receive.mock.calls[0][0].boot_id).toBe('new-boot');
});
it('ignores callbacks from a retired socket after reconnect', async () => {
  const receive = vi.fn();
  stop = subscribeLive(receive, vi.fn());
  await vi.advanceTimersByTimeAsync(1);
  const old = FakeSocket.instances[0];
  old.onclose?.();
  await vi.advanceTimersByTimeAsync(1000);
  const current = FakeSocket.instances[1];
  current.onmessage?.({ data: JSON.stringify({ ...empty, sequence: 1 }) });
  old.onmessage?.({ data: JSON.stringify({ ...empty, sequence: 999 }) });
  old.onerror?.();
  await vi.advanceTimersByTimeAsync(1000);
  expect(FakeSocket.instances).toHaveLength(2);
  expect(receive.mock.calls.at(-1)![0].sequence).toBe(1);
});
it('rejects snapshots from an already retired boot', async () => {
  const receive = vi.fn();
  stop = subscribeLive(receive, vi.fn());
  await vi.advanceTimersByTimeAsync(1);
  const current = FakeSocket.instances[0];
  current.onmessage?.({ data: JSON.stringify({ ...empty, boot_id: 'first' }) });
  current.onmessage?.({
    data: JSON.stringify({ ...empty, boot_id: 'second' }),
  });
  current.onmessage?.({
    data: JSON.stringify({ ...empty, boot_id: 'first', sequence: 999 }),
  });
  expect(receive.mock.calls.at(-1)![0].boot_id).toBe('second');
});
