// What an active alert says, in words, wherever it is shown.
import type { AlertData } from '../api/alerts';
import { formatDeviceTime } from '../api/time';

export type ActiveAlert = AlertData['active'][number];

export function alertTitle(item: ActiveAlert): string {
  if ('rom_id' in item) {
    if (item.state === 'high') return `${item.name} is too warm`;
    if (item.state === 'low') return `${item.name} is too cold`;
    return `${item.name} is disconnected`;
  }
  return item.state === 'bus_fault' ? 'Sensor bus fault' : 'Wi-Fi disconnected';
}

export function alertDetail(item: ActiveAlert): string | null {
  if ('rom_id' in item) return null;
  return item.state === 'bus_fault'
    ? 'Probes on the shared bus may read as missing.'
    : 'The device has lost its network connection.';
}

// When the condition began: the wall-clock time once the clock is trusted, otherwise the time since
// this boot started, as on the Events tab.
export function alertSince(item: ActiveAlert, timezone: string | null): string {
  return item.since_utc_seconds === null
    ? `Since boot +${(item.since_uptime_ms / 1000).toFixed(0)} seconds`
    : `Since ${formatDeviceTime(item.since_utc_seconds * 1000, timezone)}`;
}
