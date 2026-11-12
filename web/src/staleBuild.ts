// After a firmware update the device serves a new web build whose files have new names. A tab
// opened before the update still asks for the old names when it first shows a page (Settings,
// History, ...), gets 404 and cannot draw it. Vite reports that as `vite:preloadError`; reload once
// to fetch the new build. The reload is not repeated within a minute, so a real outage does not
// loop; the error screen handles it instead.
const reloadKey = 'temperature-monitor:stale-build-reload';

export function reloadOnStaleBuild(
  target: Pick<Window, 'addEventListener'> = window,
  storage: Pick<Storage, 'getItem' | 'setItem'> | undefined = safeSession(),
  reload: () => void = () => location.reload(),
  now: () => number = Date.now,
): void {
  target.addEventListener('vite:preloadError', (event) => {
    const last = Number(storage?.getItem(reloadKey) ?? 0);
    if (now() - last < 60000) return; // already reloaded just now: let the error show
    try {
      storage?.setItem(reloadKey, String(now()));
    } catch {
      // Storage may be unavailable (private mode); reloading is still right once.
    }
    event.preventDefault();
    reload();
  });
}

function safeSession(): Storage | undefined {
  try {
    return window.sessionStorage;
  } catch {
    return undefined;
  }
}
