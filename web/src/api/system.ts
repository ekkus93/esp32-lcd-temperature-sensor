import { request } from './auth';
import {
  systemFactoryResetValid,
  systemRebootValid,
} from './schema-validators.generated';

const validate = systemRebootValid;
const validateReset = systemFactoryResetValid;

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
