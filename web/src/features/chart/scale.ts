// Scales for the trend chart: round temperature bounds and readable time ticks.

export interface TimeTick {
  at: number;
  label: string;
}

// Round bounds and a step giving three to six intervals.
export function niceScale(values: number[]): {
  min: number;
  max: number;
  step: number;
} {
  let lo = Math.min(...values);
  let hi = Math.max(...values);
  if (!Number.isFinite(lo) || !Number.isFinite(hi))
    return { min: 0, max: 4, step: 1 };
  if (hi - lo < 2) {
    const middle = (lo + hi) / 2;
    lo = middle - 1;
    hi = middle + 1;
  }
  for (const step of [0.5, 1, 2, 5, 10, 20, 50]) {
    const min = Math.floor(lo / step) * step;
    const max = Math.ceil(hi / step) * step;
    if ((max - min) / step <= 6) return { min, max, step };
  }
  return { min: lo, max: hi, step: hi - lo };
}

const HOUR = 3_600_000;
const QUARTER = 15 * 60_000;

// Ticks across [from, to]. With a trusted clock they fall on round local times in the device's
// time zone (every 4 hours for a day, each midnight for a week, every fifth midnight for a month)
// and read as times or dates; without one they count back from now ("20 h ago") in a step that
// suits the time since boot.
export function timeTicks(
  from: number,
  to: number,
  basis: 'utc' | 'uptime',
  timezone: string | null,
): TimeTick[] {
  const span = to - from;
  const kind =
    span <= 2 * 24 * HOUR ? 'hours' : span <= 8 * 24 * HOUR ? 'days' : 'weeks';
  if (basis === 'uptime') {
    // Since boot: a round step giving at most six intervals, counted back from now.
    const minutes = [15, 30, 60, 120, 240, 720, 1440, 2880, 7200];
    const step =
      (minutes.find((m) => span / (m * 60_000) <= 6) ?? 7200) * 60_000;
    const ago = (ms: number) =>
      ms < HOUR
        ? `${Math.round(ms / 60_000)} min ago`
        : ms < 48 * HOUR
          ? `${Math.round(ms / HOUR)} h ago`
          : `${Math.round(ms / (24 * HOUR))} d ago`;
    const ticks: TimeTick[] = [];
    for (let back = 0; to - back >= from; back += step)
      ticks.push({ at: to - back, label: back === 0 ? 'Now' : ago(back) });
    return ticks.reverse();
  }
  const zone = timezone ?? 'UTC';
  const parts = new Intl.DateTimeFormat('en-GB', {
    timeZone: zone,
    hour: '2-digit',
    minute: '2-digit',
    hourCycle: 'h23',
  });
  const label = new Intl.DateTimeFormat(undefined, {
    timeZone: zone,
    ...(kind === 'hours'
      ? { hour: '2-digit', minute: '2-digit', hourCycle: 'h23' }
      : kind === 'days'
        ? { weekday: 'short' }
        : { month: 'short', day: 'numeric' }),
  } as Intl.DateTimeFormatOptions);
  // Local round times can fall on any quarter hour in UTC (zones such as +05:30 or +05:45), so
  // check each quarter hour in the range.
  const ticks: TimeTick[] = [];
  let midnights = 0;
  for (let at = Math.ceil(from / QUARTER) * QUARTER; at <= to; at += QUARTER) {
    const time = parts.formatToParts(new Date(at));
    const hour = Number(time.find((part) => part.type === 'hour')?.value);
    const minute = Number(time.find((part) => part.type === 'minute')?.value);
    if (minute !== 0) continue;
    if (kind === 'hours' ? hour % 4 !== 0 : hour !== 0) continue;
    if (kind === 'weeks' && midnights++ % 5 !== 0) continue;
    ticks.push({ at, label: label.format(new Date(at)) });
  }
  return ticks;
}
