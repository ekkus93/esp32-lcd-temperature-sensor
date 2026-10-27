import Ajv from 'ajv';
import schema from '../../../contracts/history.schema.json';
import type { HistoryResult } from './api-history.generated';
import { request } from './auth';
import type { Snapshot } from './generated';
export type { HistoryResult } from './api-history.generated';
const validate = new Ajv().compile<HistoryResult>(schema);
function historyQuery(
  snapshot: Snapshot,
  hours: number,
  maxPoints?: number,
): URLSearchParams {
  const synchronized =
    snapshot.clock.synchronized && snapshot.clock.utc !== null;
  const to = synchronized
    ? Date.parse(snapshot.clock.utc!)
    : snapshot.uptime_ms;
  if (!Number.isFinite(to)) throw new Error('Invalid device clock');
  const query = new URLSearchParams({
    from: String(Math.max(0, to - hours * 3600000)),
    to: String(Math.max(1, to)),
  });
  if (maxPoints !== undefined) query.set('max_points', String(maxPoints));
  if (!synchronized) query.set('boot_id', snapshot.boot_id);
  return query;
}
export function historyExportUrl(
  snapshot: Snapshot,
  hours: number,
): string | undefined {
  try {
    return '/api/v1/history/export?' + historyQuery(snapshot, hours).toString();
  } catch {
    return undefined;
  }
}
export async function getHistory(
  snapshot: Snapshot,
  hours: number,
): Promise<HistoryResult> {
  const query = historyQuery(snapshot, hours, 120);
  const value = await request<unknown>('/history?' + query.toString());
  if (
    !validate(value) ||
    value.points.some(
      (p) =>
        p.start_ms >= p.end_ms ||
        p.minimum_c > p.average_c ||
        p.average_c > p.maximum_c,
    ) ||
    value.gaps.some((gap) => gap.start_ms >= gap.end_ms)
  )
    throw new Error('Invalid history response');
  return value;
}
