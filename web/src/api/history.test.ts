import { afterEach, expect, it, vi } from 'vitest';
import { getHistory, historyExportUrl } from './history';
import { empty as fixture } from './fixtures';
import { parseSnapshot } from './client';
const empty = parseSnapshot(fixture);
it('exports full-resolution records with explicit boot scope and no graph downsampling limit', () => {
  const url = historyExportUrl({ ...empty, uptime_ms: 7200000 }, 1);
  expect(url).toContain('/api/v1/history/export?');
  expect(url).toContain('boot_id=');
  expect(url).toContain('from=3600000&to=7200000');
  expect(url).not.toContain('max_points');
});
it('keeps an invalid clock from producing an unsafe export URL', () => {
  expect(
    historyExportUrl(
      { ...empty, clock: { synchronized: true, utc: 'invalid' } },
      1,
    ),
  ).toBeUndefined();
});
afterEach(() => vi.unstubAllGlobals());
const response = {
  schema_version: 1,
  simulated: false,
  truncated: false,
  time_basis: 'uptime',
  points: [],
  gaps: [],
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
it('preserves explicit missing intervals in history responses', async () => {
  const gap = {
    boot_id: '0123456789abcdef',
    rom_id: '2801000000000029',
    start_ms: 300000,
    end_ms: 600000,
  };
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ ...response, gaps: [gap] }),
    }),
  );
  expect(
    (
      await getHistory({ ...empty, uptime_ms: 7200000 }, 1).then((r) => r.gaps)
    ).at(0),
  ).toEqual(gap);
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
