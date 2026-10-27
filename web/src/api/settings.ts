import Ajv from 'ajv';
import patchSchema from '../../../contracts/device-settings-patch.schema.json';
import schema from '../../../contracts/device-settings.schema.json';
import type { DeviceSettings } from './api-device-settings.generated';
import type { DeviceSettingsPatch } from './api-device-settings-patch.generated';
import { request } from './auth';
export type { DeviceSettings } from './api-device-settings.generated';
const validate = new Ajv().compile<DeviceSettings>(schema);
const validatePatch = new Ajv().compile<DeviceSettingsPatch>(patchSchema);
function parse(value: unknown): DeviceSettings {
  if (!validate(value)) throw new Error('Invalid device settings response');
  return value;
}
export async function getSettings() {
  return parse(await request<unknown>('/settings'));
}
export type SettingsPatch = DeviceSettingsPatch;
export async function saveSettings(patch: SettingsPatch, etag: string) {
  if (!validatePatch(patch)) throw new Error('Invalid device settings update');
  return parse(
    await request<unknown>('/settings', 'PATCH', patch, { 'If-Match': etag }),
  );
}
export const timezones = schema.properties.timezone.enum;
