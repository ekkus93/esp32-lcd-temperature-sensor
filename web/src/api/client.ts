import Ajv from 'ajv';
import schema from '../../../contracts/snapshot.schema.json';
import type { Snapshot } from './generated';
const validate = new Ajv({ strict: true }).compile<Snapshot>(schema);
export function parseSnapshot(value: unknown): Snapshot {
  if (!validate(value)) throw new Error('Invalid device snapshot');
  const ids = value.sensors.map((s) => s.rom_id);
  if (new Set(ids).size !== ids.length)
    throw new Error('Duplicate sensor identity');
  for (const s of value.sensors) {
    if (
      !['normal', 'high', 'low'].includes(s.state) &&
      s.temperature_c !== null
    )
      throw new Error('Invalid sensor state carries a current reading');
  }
  if (value.clock.synchronized && value.clock.utc === null)
    throw new Error('Synchronized clock has no UTC');
  return value;
}
export async function getSnapshot(signal?: AbortSignal): Promise<Snapshot> {
  const response = await fetch('/api/v1/status', {
    credentials: 'same-origin',
    signal,
  });
  if (response.status === 401) window.dispatchEvent(new Event('auth-required'));
  if (!response.ok)
    throw new Error(`Device request failed (${response.status})`);
  return parseSnapshot(await response.json());
}
export function temperature(
  value: number | null,
  unit: 'C' | 'F' = 'C',
): string {
  if (value === null || !Number.isFinite(value)) return '—';
  return `${(unit === 'F' ? value * 1.8 + 32 : value).toFixed(1)} °${unit}`;
}
