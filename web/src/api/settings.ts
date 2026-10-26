import Ajv from 'ajv';
import schema from '../../../contracts/device-settings.schema.json';
import { request } from './auth';
export interface DeviceSettings {
  schema_version: 1;
  etag: string;
  revision: number;
  writable: boolean;
  units: 'C' | 'F';
  display_mode: 'basic' | 'extended';
  overview_seconds: number;
  trends_seconds: number;
  lookback_minutes: number;
  timezone: string;
  ntp_server: string;
}
const validate = new Ajv().compile<DeviceSettings>(schema);
function parse(value: unknown): DeviceSettings {
  if (!validate(value)) throw new Error('Invalid device settings response');
  return value;
}
export async function getSettings() {
  return parse(await request<unknown>('/settings'));
}
export type SettingsPatch = Omit<
  DeviceSettings,
  'schema_version' | 'etag' | 'revision' | 'writable'
>;
export async function saveSettings(patch: SettingsPatch, etag: string) {
  return parse(
    await request<unknown>('/settings', 'PATCH', patch, { 'If-Match': etag }),
  );
}
export const timezones = schema.properties.timezone.enum;
