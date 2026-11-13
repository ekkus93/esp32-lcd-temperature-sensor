// The arithmetic and guards of the ice-bath calibration (spec §17.2). Nothing here touches the
// device or the page, so it is tested on its own.

// One minute of readings at the 5-second cycle, all within two of the probe's 0.0625 °C steps.
export const STEADY_SAMPLES = 12;
export const STEADY_SPAN_C = 0.125;
// Measuring gives up after this long without a steady reading.
export const GIVE_UP_MS = 10 * 60 * 1000;
// The most a calibration offset may be (the probe's memory holds it; spec §17.1).
export const LIMIT_C = 12.7;
// A genuine probe is within ±0.5 °C; an offset beyond this asks for a check first.
export const WARN_C = 2;

// Whether the latest readings have settled: the last STEADY_SAMPLES span no more than the limit.
export function isSteady(readings: number[]): boolean {
  if (readings.length < STEADY_SAMPLES) return false;
  const last = readings.slice(-STEADY_SAMPLES);
  return Math.max(...last) - Math.min(...last) <= STEADY_SPAN_C + 1e-9;
}

// The offset that makes the probe read 0 °C: the negative of the mean uncalibrated reading, rounded
// to the 0.1 °C step the probe's memory holds, so the monitor and the probe's copy agree exactly.
export function iceOffset(readings: number[]): number {
  const last = readings.slice(-STEADY_SAMPLES);
  const mean = last.reduce((sum, value) => sum + value, 0) / last.length;
  const rounded = Math.round(-mean * 10) / 10;
  return rounded === 0 ? 0 : rounded; // no -0
}

// The uncalibrated reading behind a live (calibrated) one.
export function rawReading(temperatureC: number, calibrationC: number): number {
  return temperatureC - calibrationC;
}

export type OffsetVerdict =
  | { kind: 'refuse'; reason: string }
  | { kind: 'warn'; reason: string }
  | { kind: 'ok' };

// What may be saved for an offset. Beyond the limit it is refused: the probe or the bath is wrong,
// and the monitor cannot hold such an offset. Beyond WARN_C it is allowed only after a check.
export function judgeOffset(offsetC: number): OffsetVerdict {
  if (Math.abs(offsetC) > LIMIT_C + 1e-9)
    return {
      kind: 'refuse',
      reason: `A correction of ${Math.abs(offsetC).toFixed(1)} °C is outside the ±${LIMIT_C} °C the probe can hold. The probe or the ice bath is wrong. Nothing was saved.`,
    };
  if (Math.abs(offsetC) > WARN_C)
    return {
      kind: 'warn',
      reason: `A correction of ${Math.abs(offsetC).toFixed(1)} °C is much larger than a genuine probe's ±0.5 °C error. Check the ice bath (crushed ice, stirred, probe covered) and the probe before saving.`,
    };
  return { kind: 'ok' };
}
