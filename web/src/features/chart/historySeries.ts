// Turns a history answer into chart lines and shaded gaps, the same way on the Dashboard and the
// History page.
import type { HistoryResult } from '../../api/history';
import { formatDeviceTime } from '../../api/time';
import type { TrendGap, TrendSeries } from './TrendChart';

export interface ChartProbe {
  rom: string;
  name: string;
  colour: string;
}

const minutes = (ms: number) => `${Math.round(ms / 60000)} min`;

// A moment on the chart, in the history's time basis.
export function pointTime(
  at: number,
  basis: HistoryResult['time_basis'],
  timezone: string | null,
): string {
  return basis === 'utc'
    ? formatDeviceTime(at, timezone)
    : `${minutes(at)} since boot`;
}

// A readable description of a missing interval, in the same basis as the chart.
export function gapText(
  start: number,
  end: number,
  basis: HistoryResult['time_basis'],
  timezone: string | null,
): string {
  const missing = `${minutes(end - start)} missing`;
  return basis === 'utc'
    ? `${formatDeviceTime(start, timezone)} to ${formatDeviceTime(end, timezone)} (${missing})`
    : `${minutes(start)} to ${minutes(end)} since boot (${missing})`;
}

// Where a probe's readings stop for this long, its line crosses the hole as a dashed bridge. A
// shorter dropout is joined by the curve, with the missing interval shaded.
export const lineBreakMs = 30 * 60_000;

// One line per probe, in runs. Without a trusted clock each boot is its own run, since one boot's
// elapsed times cannot be compared with another's; with one, a probe's boots share a time axis.
// Either way a point after lineBreakMs or more without readings is marked afterGap.
export function chartSeries(
  result: HistoryResult,
  probes: ChartProbe[],
): TrendSeries[] {
  return probes.map((probe) => {
    const points = result.points
      .filter((point) => point.rom_id === probe.rom)
      .sort((a, b) => a.start_ms - b.start_ms);
    return {
      key: probe.rom,
      name: probe.name,
      colour: probe.colour,
      points: points.map((point, i) => {
        const previous = points[i - 1];
        return {
          start: point.start_ms,
          end: point.end_ms,
          celsius: point.average_c,
          run: result.time_basis === 'utc' ? 'utc' : point.boot_id,
          afterGap:
            previous !== undefined &&
            point.start_ms - previous.end_ms >= lineBreakMs,
        };
      }),
    };
  });
}

// Missing intervals of the probes shown, each shaded once whichever probe it belongs to.
export function chartGaps(
  result: HistoryResult,
  roms: string[],
  timezone: string | null,
): TrendGap[] {
  const seen = new Set<string>();
  return result.gaps
    .filter((gap) => roms.includes(gap.rom_id))
    .filter((gap) => {
      const key = `${gap.boot_id}:${gap.start_ms}:${gap.end_ms}`;
      if (seen.has(key)) return false;
      seen.add(key);
      return true;
    })
    .map((gap) => ({
      start: gap.start_ms,
      end: gap.end_ms,
      label: gapText(gap.start_ms, gap.end_ms, result.time_basis, timezone),
    }));
}
