import Ajv from 'ajv';
import patchSchema from '../../../contracts/sensor-patch.schema.json';
import schema from '../../../contracts/sensor-configurations.schema.json';
import storageSchema from '../../../contracts/sensor-storage.schema.json';
import retirementSchema from '../../../contracts/sensor-retire.schema.json';
import type { SensorPatch } from './api-sensor-patch.generated';
import type { SensorConfigurations } from './api-sensors.generated';
import type { SensorStorageStatus } from './api-sensor-storage.generated';
import type { SensorRetirementAcceptance } from './api-sensor-retire.generated';
import { request } from './auth';
export type SensorConfiguration = SensorConfigurations[number];
const validate = new Ajv().compile<SensorConfigurations>(schema);
const validatePatch = new Ajv().compile<SensorPatch>(patchSchema);
const validateStorage = new Ajv().compile<SensorStorageStatus>(storageSchema);
const validateRetirement = new Ajv().compile<SensorRetirementAcceptance>(
  retirementSchema,
);
export async function getSensors(): Promise<SensorConfigurations> {
  const result = await request<unknown>('/sensors');
  if (
    !validate(result) ||
    new Set(result.map((s) => s.rom_id)).size !== result.length
  )
    throw new Error('Invalid sensor configuration response');
  return result;
}
export async function getSensorStorageStatus(): Promise<SensorStorageStatus> {
  const result = await request<unknown>('/sensors/storage');
  if (!validateStorage(result))
    throw new Error('Invalid sensor storage status response');
  return result;
}
export async function saveSensor(id: string, patch: SensorPatch, etag: string) {
  if (!validatePatch(patch)) throw new Error('Invalid sensor update');
  return request(`/sensors/${encodeURIComponent(id)}`, 'PATCH', patch, {
    'If-Match': etag,
  });
}
export async function retireSensor(
  id: string,
  etag: string,
): Promise<SensorRetirementAcceptance> {
  const result = await request<unknown>(
    `/sensors/${encodeURIComponent(id)}`,
    'DELETE',
    undefined,
    { 'If-Match': etag },
  );
  if (!validateRetirement(result))
    throw new Error('Invalid sensor retirement response');
  return result;
}
