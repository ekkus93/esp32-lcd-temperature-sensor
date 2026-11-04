import Ajv from 'ajv';
import schema from '../../../contracts/clock-diagnostics.schema.json';
import type { ClockDiagnostics } from './api-clock-diagnostics.generated';
import { request } from './auth';

export type { ClockDiagnostics };
const validate = new Ajv().compile<ClockDiagnostics>(schema);

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
