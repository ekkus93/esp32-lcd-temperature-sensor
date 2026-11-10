import type { Alerts } from './api-alerts.generated';
import { request } from './auth';
import { alertsValid } from './schema-validators.generated';
export type AlertData = Alerts;
const validate = alertsValid;
export async function getAlerts(after?: number, limit = 50): Promise<Alerts> {
  const query = new URLSearchParams();
  if (after !== undefined) query.set('after', String(after));
  query.set('limit', String(limit));
  const value = await request<unknown>(`/alerts?${query.toString()}`);
  if (!validate(value)) throw new Error('Invalid alert response');
  return value;
}
