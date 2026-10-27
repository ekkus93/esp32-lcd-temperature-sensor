import Ajv from 'ajv';
import schema from '../../../contracts/system-reboot.schema.json';
import { request } from './auth';
import type { SystemRebootAcceptance } from './api-system-reboot.generated';
import resetSchema from '../../../contracts/system-factory-reset.schema.json';
import type { SystemFactoryResetAcceptance } from './api-system-factory-reset.generated';

const validate = new Ajv().compile<SystemRebootAcceptance>(schema);
const validateReset = new Ajv().compile<SystemFactoryResetAcceptance>(
  resetSchema,
);

export async function rebootDevice(): Promise<void> {
  const result = await request<unknown>('/system/reboot', 'POST');
  if (!validate(result)) throw new Error('Unexpected restart response');
}

export async function factoryResetDevice(eraseHistory: boolean): Promise<void> {
  const result = await request<unknown>('/system/factory-reset', 'POST', {
    erase_history: eraseHistory,
  });
  if (!validateReset(result))
    throw new Error('Unexpected factory reset response');
}
