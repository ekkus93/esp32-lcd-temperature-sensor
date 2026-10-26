import { getSnapshot, parseSnapshot } from './client';
import type { Snapshot } from './generated';
// Full snapshots keep reconnect recovery bounded; sequence gaps still trigger REST resync.
export function subscribeLive(
  onSnapshot: (snapshot: Snapshot) => void,
  onError: (message: string) => void,
): () => void {
  const abort = new AbortController();
  let socket: WebSocket | null = null;
  let stopped = false;
  let timer: ReturnType<typeof setTimeout> | undefined;
  let attempt = 0;
  let last: Snapshot | undefined;
  const retiredBoots: string[] = [];
  let lastMessage = Date.now();
  let refreshing = false;
  function accept(next: Snapshot, rest = false, baseline?: Snapshot): boolean {
    if (retiredBoots.includes(next.boot_id)) return false;
    if (last) {
      if (next.boot_id === last.boot_id && next.sequence < last.sequence)
        return false;
      // A delayed REST response must not undo a reboot already seen over WS.
      if (
        rest &&
        next.boot_id !== last.boot_id &&
        (!baseline || baseline.boot_id !== last.boot_id)
      )
        return false;
      if (next.boot_id !== last.boot_id) {
        retiredBoots.push(last.boot_id);
        if (retiredBoots.length > 32) retiredBoots.shift();
      }
    }
    last = next;
    onSnapshot(next);
    return true;
  }
  async function refresh() {
    if (refreshing || stopped) return;
    refreshing = true;
    const baseline = last;
    const request = new AbortController();
    const cancel = () => request.abort();
    abort.signal.addEventListener('abort', cancel, { once: true });
    const timeout = setTimeout(() => request.abort(), 5000);
    try {
      const next = await getSnapshot(request.signal);
      if (!stopped) accept(next, true, baseline);
    } catch (e) {
      if (!stopped)
        onError(e instanceof Error ? e.message : 'Live connection lost');
    } finally {
      clearTimeout(timeout);
      abort.signal.removeEventListener('abort', cancel);
      refreshing = false;
    }
  }
  function connect() {
    if (stopped) return;
    const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const current = new WebSocket(`${scheme}//${location.host}/api/v1/live`);
    socket = current;
    let retryScheduled = false;
    current.onopen = () => {
      if (stopped || socket !== current) return;
      attempt = 0;
      lastMessage = Date.now();
    };
    current.onmessage = (event) => {
      if (stopped || socket !== current) return;
      try {
        if (typeof event.data !== 'string' || event.data.length > 16384)
          throw new Error('Invalid live message size');
        const next = parseSnapshot(JSON.parse(event.data));
        const gap =
          last &&
          next.boot_id === last.boot_id &&
          next.sequence > last.sequence + 1;
        if (!accept(next)) return;
        lastMessage = Date.now();
        if (gap) void refresh();
      } catch {
        onError('Invalid live update; resynchronizing');
        void refresh();
      }
    };
    current.onerror = () => current.close();
    current.onclose = () => {
      if (stopped || socket !== current || retryScheduled) return;
      retryScheduled = true;
      onError('Live connection lost; readings are stale');
      const wait = Math.min(30000, 500 * 2 ** Math.min(attempt++, 6));
      timer = setTimeout(
        () => {
          void refresh();
          connect();
        },
        wait + Math.random() * 250,
      );
    };
  }
  void refresh();
  connect();
  const heartbeat = setInterval(() => {
    if (Date.now() - lastMessage > 15000) {
      onError('Live connection timed out; readings are stale');
      socket?.close();
    }
  }, 1000);
  return () => {
    stopped = true;
    abort.abort();
    clearTimeout(timer);
    clearInterval(heartbeat);
    if (socket) {
      socket.onclose = null;
      socket.close();
    }
  };
}
