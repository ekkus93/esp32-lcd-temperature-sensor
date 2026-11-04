export function formatDeviceTime(timestampMs: number, timezone: string | null) {
  const zone = timezone ?? 'UTC';
  const formatted = new Intl.DateTimeFormat('en-US', {
    month: 'short',
    day: 'numeric',
    year: 'numeric',
    hour: 'numeric',
    minute: '2-digit',
    timeZoneName: 'short',
    timeZone: zone,
  }).format(timestampMs);
  return zone === 'UTC' ? formatted : `${formatted} (${zone})`;
}
