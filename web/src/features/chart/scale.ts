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
const MINUTE = 60_000;
const QUARTER = 15 * MINUTE;
const FIVE = 5 * MINUTE;
const MAX_INTERVALS = 6;
// Local clock steps for windows up to two days; each divides a day, so they stay on round times.
const CLOCK_STEPS = [5, 15, 30, 60, 120, 180, 240, 360, 720];
// Since-boot steps, in minutes.
const UPTIME_STEPS = [1, 5, 10, 15, 30, 60, 120, 240, 720, 1440, 2880, 7200];
// Longer windows are not drawn by the app (history is limited to 31 days); beyond this only the ends
// are labelled, so the work stays bounded.
const MAX_SCANNED_SPAN = 62 * 24 * HOUR;

// Ticks across [from, to]: finite, ordered, unique and inside the window, and at least one for any
// positive window. With a trusted clock they fall on round local times in the device's time zone,
// at a step that suits the span (five minutes for a short zoom, a quarter hour for an hour, up to twelve hours for two days; each
// midnight for a week, every fifth midnight for a month). When no round time falls inside, the two
// ends are labelled instead. A local time that repeats when clocks go back is shown with its zone
// abbreviation. Without a trusted clock they count back from now ("20 h ago").
export function timeTicks(
  from: number,
  to: number,
  basis: 'utc' | 'uptime',
  timezone: string | null,
): TimeTick[] {
  if (!Number.isFinite(from) || !Number.isFinite(to) || to <= from) return [];
  const span = to - from;
  if (basis === 'uptime') {
    // Since boot: a round step giving at most six intervals, counted back from now.
    const step =
      (UPTIME_STEPS.find((m) => span / (m * MINUTE) <= MAX_INTERVALS) ??
        UPTIME_STEPS[UPTIME_STEPS.length - 1]) * MINUTE;
    const ago = (ms: number) =>
      ms >= 48 * HOUR
        ? `${Math.round(ms / (24 * HOUR))} d ago`
        : ms % HOUR === 0
          ? `${ms / HOUR} h ago`
          : `${Math.round(ms / MINUTE)} min ago`;
    const ticks: TimeTick[] = [];
    for (let back = 0; to - back >= from; back += step)
      ticks.push({ at: to - back, label: back === 0 ? 'Now' : ago(back) });
    return ticks.reverse();
  }
  const zone = timezone ?? 'UTC';
  const kind =
    span <= 2 * 24 * HOUR ? 'hours' : span <= 8 * 24 * HOUR ? 'days' : 'weeks';
  const clock: Intl.DateTimeFormatOptions = {
    timeZone: zone,
    hour: '2-digit',
    minute: '2-digit',
    hourCycle: 'h23',
  };
  const parts = new Intl.DateTimeFormat('en-GB', clock);
  const label = new Intl.DateTimeFormat(undefined, {
    timeZone: zone,
    ...(kind === 'hours'
      ? { hour: '2-digit', minute: '2-digit', hourCycle: 'h23' }
      : kind === 'days'
        ? { weekday: 'short' }
        : { month: 'short', day: 'numeric' }),
  } as Intl.DateTimeFormatOptions);
  const localMinute = (at: number) => {
    const time = parts.formatToParts(new Date(at));
    return (
      Number(time.find((part) => part.type === 'hour')?.value) * 60 +
      Number(time.find((part) => part.type === 'minute')?.value)
    );
  };
  const ticks: TimeTick[] = [];
  if (span <= MAX_SCANNED_SPAN) {
    // Local round times can fall on any quarter (or, for a short step, five minutes) in UTC, as in
    // zones such as +05:30 or +05:45, so each candidate instant is checked in local time. Walking
    // real instants never produces a local time that does not exist (spring forward).
    const stepMinutes =
      kind === 'hours'
        ? (CLOCK_STEPS.find((m) => span / (m * MINUTE) <= MAX_INTERVALS) ?? 720)
        : 1440;
    const grain = stepMinutes < 15 ? FIVE : QUARTER;
    let midnights = 0;
    for (let at = Math.ceil(from / grain) * grain; at <= to; at += grain) {
      if (localMinute(at) % stepMinutes !== 0) continue;
      if (kind === 'weeks' && midnights++ % 5 !== 0) continue;
      ticks.push({ at, label: label.format(new Date(at)) });
    }
  }
  // No round time inside (a very short zoom, or an unscanned span): label the ends.
  if (ticks.length === 0) {
    const ends = new Intl.DateTimeFormat(
      undefined,
      kind === 'hours'
        ? clock
        : { timeZone: zone, month: 'short', day: 'numeric' },
    );
    ticks.push({ at: from, label: ends.format(new Date(from)) });
    ticks.push({ at: to, label: ends.format(new Date(to)) });
  }
  // When clocks go back, a local time is shown twice; tell the two apart by their zone names.
  const seen = new Map<string, number>();
  for (const tick of ticks)
    seen.set(tick.label, (seen.get(tick.label) ?? 0) + 1);
  if (kind === 'hours' && [...seen.values()].some((count) => count > 1)) {
    const named = new Intl.DateTimeFormat(undefined, {
      ...clock,
      timeZoneName: 'short',
    });
    for (const tick of ticks)
      if ((seen.get(tick.label) ?? 0) > 1)
        tick.label = named.format(new Date(tick.at));
  }
  return ticks;
}
