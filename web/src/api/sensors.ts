import Ajv from 'ajv';
import patchSchema from '../../../contracts/sensor-patch.schema.json';
import schema from '../../../contracts/sensor-configurations.schema.json';
import type { SensorPatch } from './api-sensor-patch.generated';
import type { SensorConfigurations } from './api-sensors.generated';
import { request } from './auth';
export type SensorConfiguration = SensorConfigurations[number];
const validate = new Ajv().compile<SensorConfigurations>(schema);
const validatePatch = new Ajv().compile<SensorPatch>(patchSchema);
export async function getSensors(): Promise<SensorConfigurations> {
  const result = await request<unknown>('/sensors');
  if (
    !validate(result) ||
    new Set(result.map((s) => s.rom_id)).size !== result.length
  )
    throw new Error('Invalid sensor configuration response');
  return result;
}
export async function saveSensor(id: string, patch: SensorPatch, etag: string) {
  if (!validatePatch(patch)) throw new Error('Invalid sensor update');
  return request(`/sensors/${encodeURIComponent(id)}`, 'PATCH', patch, {
    'If-Match': etag,
  });
}
