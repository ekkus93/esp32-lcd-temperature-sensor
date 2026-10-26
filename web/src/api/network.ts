import { request } from './auth';
export interface NetworkProgress {
  state: 'offline' | 'connecting' | 'connected' | 'setup';
  ip: string;
  provisioning: boolean;
  saved_credentials: boolean;
  attempt: 'idle' | 'testing' | 'succeeded' | 'failed' | 'storage_error';
  remaining_seconds: number;
  error: string;
}
export async function networkProgress(): Promise<NetworkProgress> {
  const value = await request<NetworkProgress>('/wifi/status');
  if (
    !value ||
    !['offline', 'connecting', 'connected', 'setup'].includes(value.state) ||
    !['idle', 'testing', 'succeeded', 'failed', 'storage_error'].includes(
      value.attempt,
    ) ||
    typeof value.ip !== 'string' ||
    value.ip.length > 15 ||
    typeof value.provisioning !== 'boolean' ||
    typeof value.saved_credentials !== 'boolean' ||
    typeof value.error !== 'string' ||
    value.error.length > 128 ||
    !Number.isInteger(value.remaining_seconds) ||
    value.remaining_seconds < 0 ||
    value.remaining_seconds > 25
  )
    throw new Error('Invalid network progress response');
  return value;
}
