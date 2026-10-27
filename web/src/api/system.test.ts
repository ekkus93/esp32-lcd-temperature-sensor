import { afterEach, expect, it, vi } from 'vitest';
import { setCsrf } from './auth';
import { factoryResetDevice, rebootDevice } from './system';

afterEach(() => {
  vi.unstubAllGlobals();
  vi.clearAllMocks();
});

it('sends the selected history policy with CSRF and validates reset acceptance', async () => {
  setCsrf('session-csrf');
  const fetch = vi.fn().mockResolvedValue({
    ok: true,
    status: 202,
    json: async () => ({ status: 'resetting' }),
  });
  vi.stubGlobal('fetch', fetch);

  await factoryResetDevice(true);

  expect(fetch).toHaveBeenCalledWith(
    '/api/v1/system/factory-reset',
    expect.objectContaining({
      method: 'POST',
      credentials: 'same-origin',
      headers: {
        'Content-Type': 'application/json',
        'X-CSRF-Token': 'session-csrf',
      },
      body: '{"erase_history":true}',
    }),
  );
});

it('rejects an unexpected factory reset response', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      status: 202,
      json: async () => ({ status: 'accepted' }),
    }),
  );
  await expect(factoryResetDevice(false)).rejects.toThrow(
    'Unexpected factory reset response',
  );
});

it('sends the reboot command with the current CSRF token and validates its response', async () => {
  setCsrf('session-csrf');
  const fetch = vi.fn().mockResolvedValue({
    ok: true,
    status: 202,
    json: async () => ({ status: 'rebooting' }),
  });
  vi.stubGlobal('fetch', fetch);

  await rebootDevice();

  expect(fetch).toHaveBeenCalledWith(
    '/api/v1/system/reboot',
    expect.objectContaining({
      method: 'POST',
      credentials: 'same-origin',
      headers: { 'X-CSRF-Token': 'session-csrf' },
      body: undefined,
    }),
  );
});

it('rejects an unexpected reboot response', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      status: 202,
      json: async () => ({ status: 'accepted' }),
    }),
  );
  await expect(rebootDevice()).rejects.toThrow('Unexpected restart response');
});
