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

  it('labels whole hours only when exact', () => {
    expect(timeTicks(0, 2 * HOUR, 'uptime', null).map((t) => t.label)).toEqual([
      '2 h ago',
      '90 min ago',
      '1 h ago',
      '30 min ago',
      'Now',
    ]);
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

// Review audit F5: every window gets readable labels.
describe('time ticks for any window', () => {
  const MINUTE = 60_000;
  const local = (at: number, zone: string) =>
    new Intl.DateTimeFormat('en-GB', {
      timeZone: zone,
      hour: '2-digit',
      minute: '2-digit',
      hourCycle: 'h23',
    }).format(new Date(at));
  const wellFormed = (ticks: { at: number }[], from: number, to: number) => {
    expect(ticks.length).toBeGreaterThan(0);
    for (let i = 0; i < ticks.length; i++) {
      expect(Number.isFinite(ticks[i].at)).toBe(true);
      expect(ticks[i].at).toBeGreaterThanOrEqual(from);
      expect(ticks[i].at).toBeLessThanOrEqual(to);
      if (i) expect(ticks[i].at).toBeGreaterThan(ticks[i - 1].at);
    }
  };

  it('labels one hour in Los Angeles every quarter hour (it had none)', () => {
    const from = Date.parse('2026-01-01T21:00:00Z');
    const to = Date.parse('2026-01-01T22:00:00Z');
    const ticks = timeTicks(from, to, 'utc', 'America/Los_Angeles');
    expect(ticks.map((t) => local(t.at, 'America/Los_Angeles'))).toEqual([
      '13:00',
      '13:15',
      '13:30',
      '13:45',
      '14:00',
    ]);
  });

  it('uses finer steps when zoomed in, and the ends when no round time is inside', () => {
    const from = Date.parse('2026-01-01T21:00:00Z');
    const ten = timeTicks(
      from,
      from + 10 * MINUTE,
      'utc',
      'America/Los_Angeles',
    );
    expect(ten.map((t) => local(t.at, 'America/Los_Angeles'))).toEqual([
      '13:00',
      '13:05',
      '13:10',
    ]);
    const odd = from + 61 * 1000;
    const short = timeTicks(
      odd,
      odd + 3 * MINUTE,
      'utc',
      'America/Los_Angeles',
    );
    expect(short.map((t) => t.at)).toEqual([odd, odd + 3 * MINUTE]);
    expect(short.every((t) => /\d\d:\d\d/.test(t.label))).toBe(true);
  });

  it('keeps day, two-day, week and month scales readable', () => {
    const to = Date.UTC(2026, 10, 12, 14, 7);
    expect(timeTicks(to - 24 * HOUR, to, 'utc', 'UTC')).toHaveLength(6);
    expect(timeTicks(to - 48 * HOUR, to, 'utc', 'UTC')).toHaveLength(4);
    expect(timeTicks(to - 7 * 24 * HOUR, to, 'utc', 'UTC')).toHaveLength(7);
    expect(timeTicks(to - 30 * 24 * HOUR, to, 'utc', 'UTC')).toHaveLength(6);
  });

  it('follows fractional-hour zones', () => {
    const from = Date.UTC(2026, 0, 1, 6, 0);
    for (const zone of [
      'Asia/Kathmandu',
      'Asia/Kolkata',
      'Australia/Adelaide',
    ]) {
      const ticks = timeTicks(from, from + HOUR, 'utc', zone);
      expect(ticks.length).toBeGreaterThanOrEqual(4);
      for (const tick of ticks)
        expect(Number(local(tick.at, zone).slice(3)) % 15).toBe(0);
    }
  });

  it('tells apart the hour repeated when clocks go back', () => {
    // Los Angeles, 1 November 2026: 01:00-02:00 happens in PDT and again in PST.
    const from = Date.parse('2026-11-01T07:30:00Z');
    const ticks = timeTicks(
      from,
      from + 3 * HOUR,
      'utc',
      'America/Los_Angeles',
    );
    wellFormed(ticks, from, from + 3 * HOUR);
    expect(new Set(ticks.map((t) => t.label)).size).toBe(ticks.length);
    expect(ticks.some((t) => t.label.includes('PDT'))).toBe(true);
    expect(ticks.some((t) => t.label.includes('PST'))).toBe(true);
  });

  it('never shows the hour skipped when clocks go forward', () => {
    // Los Angeles, 8 March 2026: 02:00-03:00 does not exist.
    const from = Date.parse('2026-03-08T09:00:00Z');
    const ticks = timeTicks(
      from,
      from + 3 * HOUR,
      'utc',
      'America/Los_Angeles',
    );
    wellFormed(ticks, from, from + 3 * HOUR);
    expect(
      ticks.some((t) => /^02:/.test(local(t.at, 'America/Los_Angeles'))),
    ).toBe(false);
  });

  it('gives nothing for an empty or invalid window', () => {
    expect(timeTicks(10, 10, 'utc', 'UTC')).toEqual([]);
    expect(timeTicks(20, 10, 'utc', 'UTC')).toEqual([]);
    expect(timeTicks(Number.NaN, 10, 'utc', 'UTC')).toEqual([]);
    expect(timeTicks(0, Number.POSITIVE_INFINITY, 'uptime', null)).toEqual([]);
  });

  it('counts back in minutes for a short time since boot', () => {
    expect(
      timeTicks(0, 3 * MINUTE, 'uptime', null).map((t) => t.label),
    ).toEqual(['3 min ago', '2 min ago', '1 min ago', 'Now']);
  });

  it('is well formed for many windows and zones', () => {
    const zones = [
      'UTC',
      'America/Los_Angeles',
      'Asia/Kathmandu',
      'Europe/London',
      'Pacific/Chatham',
    ];
    const spans = [
      MINUTE,
      7 * MINUTE,
      50 * MINUTE,
      3 * HOUR,
      26 * HOUR,
      47 * HOUR,
      5 * 24 * HOUR,
      31 * 24 * HOUR,
    ];
    let from = Date.parse('2026-03-01T00:00:00Z');
    for (const zone of zones)
      for (const span of spans) {
        from += 37 * MINUTE + 13_000;
        wellFormed(
          timeTicks(from, from + span, 'utc', zone),
          from,
          from + span,
        );
        wellFormed(
          timeTicks(from, from + span, 'uptime', null),
          from,
          from + span,
        );
      }
  });
});
