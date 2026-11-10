import type { ProbeScanStatus } from './api-probe-scan.generated';
import { request } from './auth';
import { probeScanValid } from './schema-validators.generated';
export type { ProbeScanStatus } from './api-probe-scan.generated';

const validate = probeScanValid;

export async function getProbeScan(): Promise<ProbeScanStatus> {
  const value = await request<unknown>('/probe-scan');
  if (!validate(value)) throw new Error('Invalid probe scan response');
  return value;
}
