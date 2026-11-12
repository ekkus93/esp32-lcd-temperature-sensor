import { useEffect, useState } from 'react';
import { getAlerts } from '../api/alerts';
import type { ActiveAlert } from '../features/alertText';

// The device's active alerts, refreshed every ten seconds, for the navigation badge and the
// Dashboard. Null until the first answer; the last good list is kept if a refresh fails.
export function useActiveAlerts(): ActiveAlert[] | null {
  const [active, setActive] = useState<ActiveAlert[] | null>(null);
  useEffect(() => {
    let live = true;
    const refresh = async () => {
      try {
        // Only the active list is needed, so ask for a single event.
        const latest = await getAlerts(undefined, 1);
        if (live) setActive(latest.active);
      } catch {
        // Keep what was shown; the Alerts page reports the error.
      }
    };
    void refresh();
    const interval = setInterval(() => void refresh(), 10000);
    return () => {
      live = false;
      clearInterval(interval);
    };
  }, []);
  return active;
}
