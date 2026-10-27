import Ajv from 'ajv';
import { request } from './auth';
import type { Snapshot } from './generated';
export interface HistoryPoint {
  boot_id: string;
  rom_id: string;
  start_ms: number;
  end_ms: number;
  utc_seconds: number | null;
  average_c: number;
  minimum_c: number;
  maximum_c: number;
  count: number;
  partial: boolean;
}
export interface HistoryResult {
  schema_version: 1;
  simulated: boolean;
  truncated: boolean;
  time_basis: 'utc' | 'uptime';
  points: HistoryPoint[];
}
const number = { type: 'number' };
const properties = {
  boot_id: { type: 'string', pattern: '^[A-Za-z0-9_-]{1,32}$' },
  rom_id: { type: 'string', pattern: '^[0-9a-f]{16}$' },
  start_ms: { type: 'integer', minimum: 0 },
  end_ms: { type: 'integer', minimum: 1 },
  utc_seconds: { type: ['integer', 'null'] },
  average_c: number,
  minimum_c: number,
  maximum_c: number,
  count: { type: 'integer', minimum: 1 },
  partial: { type: 'boolean' },
};
const validate = new Ajv().compile<HistoryResult>({
  type: 'object',
  additionalProperties: false,
  required: [
    'schema_version',
    'simulated',
    'truncated',
    'time_basis',
    'points',
  ],
  properties: {
    schema_version: { const: 1 },
    simulated: { type: 'boolean' },
    truncated: { type: 'boolean' },
    time_basis: { enum: ['utc', 'uptime'] },
    points: {
      type: 'array',
      maxItems: 2560,
      items: {
        type: 'object',
        additionalProperties: false,
        required: Object.keys(properties),
        properties,
      },
    },
  },
});
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
    )
  )
    throw new Error('Invalid history response');
  return value;
}
