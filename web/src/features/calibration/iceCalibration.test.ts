import { describe, expect, it } from 'vitest';
import {
  iceOffset,
  isSteady,
  judgeOffset,
  rawReading,
  STEADY_SAMPLES,
} from './iceCalibration';

const twelve = (base: number, step = 0) =>
  Array.from({ length: STEADY_SAMPLES }, (_, i) => base + (i % 2) * step);

describe('steady readings', () => {
  it('needs a full minute of readings within 0.125 °C', () => {
    expect(isSteady(twelve(0.2, 0.0625))).toBe(true); // exactly two steps
    expect(isSteady(twelve(0.2, 0.125))).toBe(true); // at the limit
    expect(isSteady(twelve(0.2, 0.1875))).toBe(false); // three steps
    expect(isSteady(twelve(0.2).slice(1))).toBe(false); // only eleven
  });

  it('judges only the latest minute', () => {
    const settling = [...twelve(0, 1.5), ...twelve(0.02, 0.02)];
    expect(isSteady(settling)).toBe(true);
  });
});

describe('the offset', () => {
  it('is the negative of the mean reading, rounded to 0.1 °C', () => {
    expect(iceOffset(twelve(0.44))).toBe(-0.4);
    expect(iceOffset(twelve(-0.36))).toBe(0.4);
    expect(iceOffset(twelve(0.26))).toBe(-0.3);
  });

  it('subtracts the current offset from the live reading', () => {
    // Live 0.9 °C with a saved offset of +0.5 is a raw 0.4 °C.
    expect(rawReading(0.9, 0.5)).toBeCloseTo(0.4, 10);
    expect(iceOffset(twelve(rawReading(0.9, 0.5)))).toBe(-0.4);
  });

  it('is never negative zero', () => {
    expect(Object.is(iceOffset(twelve(0)), 0)).toBe(true);
    expect(Object.is(iceOffset(twelve(0.04)), 0)).toBe(true);
  });
});

describe('the guards', () => {
  it('lets a small correction through', () => {
    expect(judgeOffset(0.4)).toEqual({ kind: 'ok' });
    expect(judgeOffset(-2)).toEqual({ kind: 'ok' });
  });

  it('asks for a check beyond 2 °C', () => {
    expect(judgeOffset(2.1).kind).toBe('warn');
    expect(judgeOffset(-5).kind).toBe('warn');
  });

  it('refuses anything beyond the ±12.7 °C limit', () => {
    expect(judgeOffset(12.7).kind).toBe('warn');
    expect(judgeOffset(12.8).kind).toBe('refuse');
    expect(judgeOffset(-13).kind).toBe('refuse');
  });
});
