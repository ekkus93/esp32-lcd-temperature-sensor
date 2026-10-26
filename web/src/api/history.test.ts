import { afterEach, expect, it, vi } from 'vitest';
import { getHistory } from './history';
import { empty as fixture } from './fixtures';
import { parseSnapshot } from './client';
const empty = parseSnapshot(fixture);
afterEach(() => vi.unstubAllGlobals());
const response = {
  schema_version: 1,
  simulated: false,
  truncated: false,
  time_basis: 'uptime',
  points: [],
};
it('queries unsynchronized data by boot and uptime without inventing UTC', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => response }),
  );
  const result = await getHistory({ ...empty, uptime_ms: 7200000 }, 1);
  expect(result.points).toEqual([]);
  expect(vi.mocked(fetch).mock.calls[0][0]).toContain(
    'from=3600000&to=7200000',
  );
  expect(vi.mocked(fetch).mock.calls[0][0]).toContain('boot_id=');
});
it('queries synchronized history using the device clock rather than the browser clock', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ ...response, time_basis: 'utc' }),
    }),
  );
  await getHistory(
    { ...empty, clock: { synchronized: true, utc: '2026-10-25T12:00:00Z' } },
    24,
  );
  const url = String(vi.mocked(fetch).mock.calls[0][0]);
  expect(url).not.toContain('boot_id=');
  expect(url).toContain('to=' + Date.parse('2026-10-25T12:00:00Z'));
});
it('rejects reversed aggregate extrema before charting', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        ...response,
        points: [
          {
            boot_id: 'test',
            rom_id: '2801000000000029',
            start_ms: 0,
            end_ms: 300000,
            utc_seconds: null,
            average_c: 20,
            minimum_c: 25,
            maximum_c: 30,
            count: 60,
            partial: false,
          },
        ],
      }),
    }),
  );
  await expect(getHistory(empty, 24)).rejects.toThrow(
    'Invalid history response',
  );
});
