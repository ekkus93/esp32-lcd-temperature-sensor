import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { subscribeLive } from './live';
import { empty } from './fixtures';
class FakeSocket {
  static instances: FakeSocket[] = [];
  onopen: (() => void) | null = null;
  onmessage: ((e: { data: unknown }) => void) | null = null;
  onclose: (() => void) | null = null;
  onerror: (() => void) | null = null;
  static OPEN = 1;
  readyState = 0;
  closeCalls = 0;
  constructor() {
    FakeSocket.instances.push(this);
  }
  open() {
    this.readyState = FakeSocket.OPEN;
    this.onopen?.();
  }
  close() {
    this.closeCalls += 1;
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
it('gives a reconnecting socket its own handshake time after a silent period', async () => {
  const error = vi.fn();
  const receive = vi.fn();
  stop = subscribeLive(receive, error);
  const first = FakeSocket.instances[0];
  first.open();
  // The device goes quiet; the heartbeat closes the stale socket and a retry starts.
  await vi.advanceTimersByTimeAsync(16000);
  expect(first.closeCalls).toBe(1);
  await vi.advanceTimersByTimeAsync(2000);
  const second = FakeSocket.instances[1];
  expect(second).toBeDefined();
  // The new socket is still connecting. It must not be killed by the old silence.
  const timeouts = () =>
    error.mock.calls.filter(([m]) => String(m).includes('timed out')).length;
  const before = timeouts();
  await vi.advanceTimersByTimeAsync(8000);
  expect(second.closeCalls).toBe(0);
  expect(FakeSocket.instances).toHaveLength(2);
  expect(timeouts()).toBe(before); // and the stale banner is not re-raised every second
  // It finally opens and starts delivering data: normal operation resumes.
  second.open();
  second.onmessage?.({ data: JSON.stringify({ ...empty, sequence: 3 }) });
  await vi.advanceTimersByTimeAsync(5000);
  expect(second.closeCalls).toBe(0);
  expect(receive).toHaveBeenCalledWith(
    expect.objectContaining({ sequence: 3 }),
  );
});
it('closes a socket that never finishes connecting after the silence limit', async () => {
  stop = subscribeLive(vi.fn(), vi.fn());
  const only = FakeSocket.instances[0];
  await vi.advanceTimersByTimeAsync(14000);
  expect(only.closeCalls).toBe(0);
  await vi.advanceTimersByTimeAsync(3000);
  expect(only.closeCalls).toBe(1);
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
  let finish!: (value: Response) => void;
  vi.mocked(fetch).mockImplementationOnce(
    () =>
      new Promise<Response>((resolve) => {
        finish = resolve;
      }),
  );
  const receive = vi.fn();
  stop = subscribeLive(receive, vi.fn());
  FakeSocket.instances[0].onmessage?.({
    data: JSON.stringify({ ...empty, boot_id: 'new-boot' }),
  });
  finish({
    ok: true,
    json: async () => ({ ...empty, boot_id: 'old-boot', sequence: 999 }),
  } as Response);
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
it('keeps showing live values when a resync fails while the socket is healthy', async () => {
  const receive = vi.fn();
  const error = vi.fn();
  stop = subscribeLive(receive, error);
  await vi.advanceTimersByTimeAsync(1);
  const socket = FakeSocket.instances[0];
  socket.open();
  socket.onmessage?.({ data: JSON.stringify({ ...empty, sequence: 1 }) });
  error.mockClear();
  vi.mocked(fetch).mockRejectedValue(new Error('Network down'));
  // A gap asks for a REST resync, which fails; live data is still arriving, so nothing is stale.
  socket.onmessage?.({ data: JSON.stringify({ ...empty, sequence: 9 }) });
  await vi.advanceTimersByTimeAsync(50);
  expect(fetch).toHaveBeenCalledTimes(2); // the resync really ran (and failed)
  expect(error).not.toHaveBeenCalled();
});
it('reports a failed first load when no live data has arrived', async () => {
  const error = vi.fn();
  vi.mocked(fetch).mockRejectedValue(new Error('Network down'));
  stop = subscribeLive(vi.fn(), error);
  await vi.advanceTimersByTimeAsync(1);
  expect(error).toHaveBeenCalledWith('Network down');
});
