import Ajv from 'ajv';
import snapshotSchema from '../../../contracts/snapshot.schema.json';
import type { Snapshot } from './generated';
import { request } from './auth';
export interface SensorConfiguration {
  rom_id: string;
  etag: string;
  revision: number;
  name: string;
  short_name: string;
  enabled: boolean;
  disconnect_alert: boolean;
  order: number;
  calibration_c: number;
  low_c: number | null;
  high_c: number | null;
  hysteresis_c: number;
  persistence_seconds: number;
  state: Snapshot['sensors'][number]['state'];
}
const properties = {
  etag: { type: 'string', minLength: 1, maxLength: 128 },
  revision: { type: 'integer', minimum: 0 },
  rom_id: snapshotSchema.definitions.sensor.properties.rom_id,
  name: snapshotSchema.definitions.sensor.properties.name,
  state: snapshotSchema.definitions.sensor.properties.state,
  short_name: { type: 'string', maxLength: 12 },
  enabled: { type: 'boolean' },
  disconnect_alert: { type: 'boolean' },
  order: { type: 'integer', minimum: 0, maximum: 4 },
  calibration_c: { type: 'number', minimum: -20, maximum: 20 },
  low_c: { type: ['number', 'null'], minimum: -55, maximum: 125 },
  high_c: { type: ['number', 'null'], minimum: -55, maximum: 125 },
  hysteresis_c: { type: 'number', minimum: 0, maximum: 20 },
  persistence_seconds: { type: 'number', minimum: 0, maximum: 3600 },
};
const validate = new Ajv().compile<SensorConfiguration[]>({
  type: 'array',
  maxItems: 5,
  items: {
    type: 'object',
    additionalProperties: false,
    required: Object.keys(properties),
    properties,
  },
});
export async function getSensors(): Promise<SensorConfiguration[]> {
  const result = await request<unknown>('/sensors');
  if (
    !validate(result) ||
    new Set(result.map((s) => s.rom_id)).size !== result.length
  )
    throw new Error('Invalid sensor configuration response');
  return result;
}
export type SensorPatch = Partial<
  Omit<SensorConfiguration, 'rom_id' | 'state' | 'etag' | 'revision'>
>;
export async function saveSensor(id: string, patch: SensorPatch, etag: string) {
  return request(`/sensors/${encodeURIComponent(id)}`, 'PATCH', patch, {
    'If-Match': etag,
  });
}
