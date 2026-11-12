import { describe, expect, it } from 'vitest';
import { roomState, type PanelProbe } from './roomState';
import type { ActiveAlert } from '../alertText';

const probe = (
  name: string,
  celsius: number | null,
  state = 'normal',
  lowC: number | null = 18,
  highC: number | null = 28,
): PanelProbe => ({ rom: `rom-${name}`, name, state, celsius, lowC, highC });

const base = {
  unit: 'C' as const,
  stale: false,
  busFault: false,
  alerts: [] as ActiveAlert[],
  uptimeMs: 3_600_000,
};

describe('the room state in words', () => {
  it('says all probes are in range, with the lowest and highest reading', () => {
    const state = roomState({
      ...base,
      probes: [
        probe('Canopy', 24.6),
        probe('Intake', 19.8),
        probe('Floor', 22.1),
      ],
    });
    expect(state.headline).toBe('All three probes are in range');
    expect(state.detail).toBe(
      'Lowest 19.8 °C at Intake, highest 24.6 °C at Canopy.',
    );
  });

  it('names a warm probe, its limit and how long it has been warm', () => {
    const alert: ActiveAlert = {
      rom_id: 'rom-Exhaust',
      name: 'Exhaust',
      state: 'high',
      since_uptime_ms: 3_600_000 - 12 * 60_000,
      since_utc_seconds: null,
    };
    const state = roomState({
      ...base,
      alerts: [alert],
      probes: [
        probe('Canopy', 24.6),
        probe('Exhaust', 29.4, 'high'),
        probe('Floor', 22.1),
        probe('Intake', 19.8),
        probe('Reservoir', 20.3),
      ],
    });
    expect(state.headline).toBe('Exhaust is running warm');
    expect(state.detail).toBe(
      '29.4 °C, above its 28.0 °C limit for 12 minutes. The other four probes are in range.',
    );
  });

  it('gives a cold probe in the display unit, without a duration when no alert is known', () => {
    const state = roomState({
      ...base,
      unit: 'F',
      probes: [probe('Floor', 10, 'low'), probe('Canopy', 24)],
    });
    expect(state.headline).toBe('Floor is running cold');
    expect(state.detail).toBe(
      '50.0 °F, below its 64.4 °F limit. The other probe is in range.',
    );
  });

  it('counts several problems and names each', () => {
    const state = roomState({
      ...base,
      probes: [
        probe('Exhaust', 29.4, 'high'),
        probe('Floor', null, 'disconnected'),
        probe('Canopy', 24),
      ],
    });
    expect(state.headline).toBe('Two probes need attention');
    expect(state.detail).toBe('Exhaust is too warm and Floor is disconnected.');
  });

  it('puts a lost live connection and a bus fault before anything else', () => {
    const probes = [probe('Exhaust', 29.4, 'high')];
    expect(roomState({ ...base, probes, stale: true }).headline).toBe(
      'Readings are out of date',
    );
    expect(roomState({ ...base, probes, busFault: true }).headline).toBe(
      'The sensor bus has a fault',
    );
  });

  it('leaves out a duration shorter than a minute', () => {
    const state = roomState({
      ...base,
      alerts: [
        {
          rom_id: 'rom-Floor',
          name: 'Floor',
          state: 'disconnected',
          since_uptime_ms: base.uptimeMs - 20_000,
          since_utc_seconds: null,
        },
      ],
      probes: [probe('Floor', null, 'disconnected'), probe('Canopy', 24)],
    });
    expect(state.detail).toBe(
      'No reading. Check its cable and connector. The other probe is in range.',
    );
  });

  it('handles a disconnected probe, one probe, and none', () => {
    expect(
      roomState({ ...base, probes: [probe('Floor', null, 'disconnected')] }),
    ).toEqual({
      headline: 'Floor is disconnected',
      detail: 'No reading. Check its cable and connector.',
    });
    expect(roomState({ ...base, probes: [probe('Canopy', 24)] }).headline).toBe(
      'The probe is in range',
    );
    expect(roomState({ ...base, probes: [] }).headline).toBe(
      'No probes detected',
    );
  });
});
