import Ajv from 'ajv';
import schema from '../../../contracts/alerts.schema.json';
import type { Alerts } from './api-alerts.generated';
import { request } from './auth';
export type AlertData = Alerts;
const validate = new Ajv().compile<Alerts>(schema);
export async function getAlerts(after?: number, limit = 50): Promise<Alerts> {
  const query = new URLSearchParams();
  if (after !== undefined) query.set('after', String(after));
  query.set('limit', String(limit));
  const value = await request<unknown>(`/alerts?${query.toString()}`);
  if (!validate(value)) throw new Error('Invalid alert response');
  return value;
}
