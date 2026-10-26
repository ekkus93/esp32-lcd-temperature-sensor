import { describe, expect, it } from 'vitest';
import { parseSnapshot, temperature } from './client';
import { empty } from './fixtures';
describe('device contract', () => {
  it('accepts no probes and unknown UTC', () =>
    expect(parseSnapshot(empty).sensors).toEqual([]));
  it('rejects fabricated history cadence and schema versions', () => {
    expect(() => parseSnapshot({ ...empty, version: 2 })).toThrow();
    expect(() =>
      parseSnapshot({
        ...empty,
        logging: { healthy: true, interval_seconds: 5 },
      }),
    ).toThrow();
  });
  it('rejects duplicate ROMs and misleading disconnected values', () => {
    const sensor = {
      rom_id: '280000000000001e',
      name: 'Room',
      enabled: true,
      order: 0,
      state: 'disconnected',
      temperature_c: 20,
      last_seen_ms: 0,
    };
    expect(() => parseSnapshot({ ...empty, sensors: [sensor] })).toThrow();
    expect(() =>
      parseSnapshot({
        ...empty,
        sensors: [
          { ...sensor, temperature_c: null },
          { ...sensor, temperature_c: null },
        ],
      }),
    ).toThrow();
  });
  it('converts only at presentation and never renders null as zero', () => {
    expect(temperature(null)).toBe('—');
    expect(temperature(0, 'F')).toBe('32.0 °F');
    expect(temperature(-20)).toBe('-20.0 °C');
  });
});
