import { afterEach, expect, it, vi } from 'vitest';
import { getProbeScan } from './probe-scan';

afterEach(() => vi.unstubAllGlobals());

it('validates the bounded authenticated probe discovery contract', async () => {
  const result = {
    schema_version: 1,
    enabled: true,
    complete: true,
    discovered_count: 6,
    overflow: true,
  };
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => result }),
  );
  expect(await getProbeScan()).toEqual(result);
  expect(vi.mocked(fetch).mock.calls[0][0]).toBe('/api/v1/probe-scan');
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ ...result, discovered_count: 17 }),
    }),
  );
  await expect(getProbeScan()).rejects.toThrow('Invalid probe scan response');
});
