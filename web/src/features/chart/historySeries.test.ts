import { expect, it } from 'vitest';
import type { HistoryResult } from '../../api/history';
import { chartSeries, lineBreakMs } from './historySeries';

const rom = '284d086700000009';
const bucket = 300_000;
const point = (boot: string, start: number) => ({
  boot_id: boot,
  rom_id: rom,
  start_ms: start,
  end_ms: start + bucket,
  utc_seconds: null,
  average_c: 20,
  minimum_c: 20,
  maximum_c: 20,
  count: 60,
  partial: false,
});
const result = (
  basis: HistoryResult['time_basis'],
  points: ReturnType<typeof point>[],
) =>
  ({
    schema_version: 1,
    simulated: false,
    truncated: false,
    time_basis: basis,
    points,
    gaps: [],
  }) as HistoryResult;
const series = (r: HistoryResult) =>
  chartSeries(r, [{ rom, name: 'Canopy', colour: '#000' }])[0].points;
const runs = (r: HistoryResult) => series(r).map((p) => p.run);

it('keeps a probe one line across reboots on the UTC axis, marking a long outage', () => {
  const t0 = 1_800_000_000_000;
  const points = series(
    result('utc', [
      point('a', t0),
      point('b', t0 + bucket), // reboot, no time lost
      point('b', t0 + 3 * bucket), // a short dropout: joined
      point('c', t0 + 4 * bucket + lineBreakMs), // a long outage: a new line
    ]),
  );
  expect(new Set(points.map((p) => p.run)).size).toBe(1);
  expect(points.map((p) => p.afterGap)).toEqual([false, false, false, true]);
});

it('keeps each boot its own line on the uptime axis', () => {
  const r = runs(result('uptime', [point('a', 0), point('b', bucket)]));
  expect(r[0]).not.toBe(r[1]);
});
