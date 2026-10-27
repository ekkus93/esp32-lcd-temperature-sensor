import Ajv from 'ajv';
import schema from '../../../contracts/system-reboot.schema.json';
import { request } from './auth';
import type { SystemRebootAcceptance } from './api-system-reboot.generated';

const validate = new Ajv().compile<SystemRebootAcceptance>(schema);

export async function rebootDevice(): Promise<void> {
  const result = await request<unknown>('/system/reboot', 'POST');
  if (!validate(result)) throw new Error('Unexpected restart response');
}
