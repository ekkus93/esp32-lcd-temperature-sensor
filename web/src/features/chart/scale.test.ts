import { describe, expect, it } from 'vitest';
import { niceScale, timeTicks } from './scale';

const HOUR = 3_600_000;

describe('chart scales', () => {
  it('rounds temperature bounds to a step with three to six intervals', () => {
    expect(niceScale([19.8, 29.4])).toEqual({ min: 18, max: 30, step: 2 });
    // A steady reading is widened to two degrees and gets half-degree lines.
    expect(niceScale([20.1, 20.3])).toEqual({ min: 19, max: 21.5, step: 0.5 });
    expect(niceScale([-20, 85])).toEqual({ min: -20, max: 100, step: 20 });
  });

  it('puts day ticks on round local hours in the device time zone', () => {
    const to = Date.UTC(2026, 10, 12, 14, 7); // 14:07 UTC
    const ticks = timeTicks(to - 24 * HOUR, to, 'utc', 'UTC');
    expect(ticks.map((t) => new Date(t.at).getUTCHours())).toEqual([
      16, 20, 0, 4, 8, 12,
    ]);
    expect(ticks.every((t) => new Date(t.at).getUTCMinutes() === 0)).toBe(true);
    // In India (+05:30) a round local hour is half past in UTC.
    const india = timeTicks(to - 24 * HOUR, to, 'utc', 'Asia/Kolkata');
    expect(new Set(india.map((t) => new Date(t.at).getUTCMinutes()))).toEqual(
      new Set([30]),
    );
    expect(india).toHaveLength(6);
  });

  it('counts back from now in a step that suits a short time since boot', () => {
    const ticks = timeTicks(0, 70 * 60_000, 'uptime', null);
    expect(ticks.map((t) => t.label)).toEqual([
      '1 h ago',
      '45 min ago',
      '30 min ago',
      '15 min ago',
      'Now',
    ]);
  });

  it('counts back from now without a trusted clock', () => {
    const ticks = timeTicks(0, 24 * HOUR, 'uptime', null);
    expect(ticks.map((t) => t.label)).toEqual([
      '24 h ago',
      '20 h ago',
      '16 h ago',
      '12 h ago',
      '8 h ago',
      '4 h ago',
      'Now',
    ]);
  });

  it('marks midnights for a week and every fifth midnight for a month', () => {
    const to = Date.UTC(2026, 10, 12, 14);
    expect(timeTicks(to - 7 * 24 * HOUR, to, 'utc', 'UTC')).toHaveLength(7);
    expect(timeTicks(to - 30 * 24 * HOUR, to, 'utc', 'UTC')).toHaveLength(6);
  });
});
