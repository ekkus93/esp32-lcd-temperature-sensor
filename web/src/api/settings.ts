import Ajv from 'ajv';
import patchSchema from '../../../contracts/device-settings-patch.schema.json';
import schema from '../../../contracts/device-settings.schema.json';
import publicSchema from '../../../contracts/device-settings-public.schema.json';
import type { DeviceSettings } from './api-device-settings.generated';
import type { DeviceSettingsPatch } from './api-device-settings-patch.generated';
import type { PublicDeviceSettings } from './api-device-settings-public.generated';
import { request } from './auth';
export type { DeviceSettings } from './api-device-settings.generated';
const validate = new Ajv().compile<DeviceSettings>(schema);
const validatePublic = new Ajv().compile<PublicDeviceSettings>(publicSchema);
const validatePatch = new Ajv().compile<DeviceSettingsPatch>(patchSchema);
function parse(value: unknown): DeviceSettings {
  if (!validate(value)) throw new Error('Invalid device settings response');
  return value;
}
export async function getSettings() {
  return parse(await request<unknown>('/settings'));
}
// The display fields anyone may read. A signed-in owner gets the full response, which carries the
// same fields plus the owner-only ones; an anonymous visitor gets the public response.
export async function getDisplaySettings(): Promise<
  Pick<PublicDeviceSettings, 'units' | 'timezone'>
> {
  const value = await request<unknown>('/settings');
  const settings = validate(value) || validatePublic(value) ? value : null;
  if (!settings) throw new Error('Invalid device settings response');
  const { units, timezone } = settings as PublicDeviceSettings;
  return { units, timezone };
}
export type SettingsPatch = DeviceSettingsPatch;
export async function saveSettings(patch: SettingsPatch, etag: string) {
  if (!validatePatch(patch)) throw new Error('Invalid device settings update');
  return parse(
    await request<unknown>('/settings', 'PATCH', patch, { 'If-Match': etag }),
  );
}
export const timezones = schema.properties.timezone.enum;
