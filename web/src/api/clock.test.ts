import { afterEach, expect, it, vi } from 'vitest';
import { getClockDiagnostics } from './clock';

afterEach(() => vi.unstubAllGlobals());

it('accepts the explicit unsynchronized state', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        schema_version: 1,
        synchronized: false,
        last_sync_utc_seconds: null,
        sync_age_ms: null,
      }),
    }),
  );
  await expect(getClockDiagnostics()).resolves.toMatchObject({
    synchronized: false,
    last_sync_utc_seconds: null,
    sync_age_ms: null,
  });
});

it('rejects an inconsistent synchronized clock state', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        schema_version: 1,
        synchronized: true,
        last_sync_utc_seconds: null,
        sync_age_ms: null,
      }),
    }),
  );
  await expect(getClockDiagnostics()).rejects.toThrow(
    'Invalid clock diagnostics response',
  );
});
