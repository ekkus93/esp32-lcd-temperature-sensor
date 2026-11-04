import Ajv from 'ajv';
import schema from '../../../contracts/probe-scan.schema.json';
import type { ProbeScanStatus } from './api-probe-scan.generated';
import { request } from './auth';
export type { ProbeScanStatus } from './api-probe-scan.generated';

const validate = new Ajv().compile<ProbeScanStatus>(schema);

export async function getProbeScan(): Promise<ProbeScanStatus> {
  const value = await request<unknown>('/probe-scan');
  if (!validate(value)) throw new Error('Invalid probe scan response');
  return value;
}
