import type { ClockDiagnostics } from './api-clock-diagnostics.generated';
import { request } from './auth';
import { clockDiagnosticsValid } from './schema-validators.generated';

export type { ClockDiagnostics };
const validate = clockDiagnosticsValid;

export async function getClockDiagnostics(): Promise<ClockDiagnostics> {
  const result = await request<unknown>('/clock');
  if (
    !validate(result) ||
    result.synchronized !==
      (result.last_sync_utc_seconds !== null && result.sync_age_ms !== null)
  )
    throw new Error('Invalid clock diagnostics response');
  return result;
}
