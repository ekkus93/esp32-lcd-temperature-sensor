// Reports a problem only after it has lasted `delayMs` without a recovery in between. A dropped live
// feed usually reconnects within seconds; showing every blip flashes a warning too fast to read.
// The message shown is the one that started the problem; later failures during it are not news.
export function delayedError(
  show: (message: string) => void,
  delayMs: number,
): {
  fail: (message: string) => void;
  recover: () => void;
  cancel: () => void;
} {
  let pending: ReturnType<typeof setTimeout> | undefined;
  const cancel = () => {
    clearTimeout(pending);
    pending = undefined;
  };
  return {
    fail(message) {
      if (pending !== undefined) return;
      pending = setTimeout(() => {
        pending = undefined;
        show(message);
      }, delayMs);
    },
    recover() {
      cancel();
      show('');
    },
    cancel,
  };
}
