import { afterEach, expect, it, vi } from 'vitest';
import { request } from './auth';

afterEach(() => {
  vi.unstubAllGlobals();
  vi.useRealTimers();
});

function stubFetch(response: Partial<Response> & { jsonBody?: unknown }) {
  const { jsonBody, ...rest } = response;
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: false,
      status: 500,
      json: async () => {
        if (jsonBody === undefined) throw new SyntaxError('Unexpected token <');
        return jsonBody;
      },
      ...rest,
    }),
  );
}

it('returns the parsed body on success', async () => {
  stubFetch({ ok: true, status: 200, jsonBody: { a: 1 } });
  await expect(request('/x')).resolves.toEqual({ a: 1 });
});

it('reports the device error message from a JSON error body', async () => {
  stubFetch({ status: 400, jsonBody: { error: 'Invalid sensor settings' } });
  await expect(request('/x')).rejects.toThrow('Invalid sensor settings');
});

it('gives a readable error when an error response is not JSON', async () => {
  stubFetch({ status: 502 });
  await expect(request('/x')).rejects.toThrow('Device request failed (502)');
});

it('still opens sign-in on a 401 whose body is not JSON', async () => {
  stubFetch({ status: 401 });
  const handler = vi.fn();
  window.addEventListener('auth-required', handler);
  await expect(request('/sensors')).rejects.toThrow(
    'Device request failed (401)',
  );
  window.removeEventListener('auth-required', handler);
  expect(handler).toHaveBeenCalledTimes(1);
});

it('does not open sign-in for a 401 from the auth endpoints themselves', async () => {
  stubFetch({ status: 401, jsonBody: { error: 'Wrong password' } });
  const handler = vi.fn();
  window.addEventListener('auth-required', handler);
  await expect(request('/auth/login', 'POST', {})).rejects.toThrow(
    'Wrong password',
  );
  window.removeEventListener('auth-required', handler);
  expect(handler).not.toHaveBeenCalled();
});

it('explains a timeout instead of showing an abort error', async () => {
  vi.useFakeTimers();
  vi.stubGlobal(
    'fetch',
    vi.fn(
      (_url: string, init: RequestInit) =>
        new Promise((_resolve, reject) => {
          init.signal?.addEventListener('abort', () =>
            reject(
              new DOMException('The operation was aborted.', 'AbortError'),
            ),
          );
        }),
    ),
  );
  const outcome = expect(request('/x')).rejects.toThrow(
    'The device did not answer in time',
  );
  await vi.advanceTimersByTimeAsync(30001);
  await outcome;
});

it('explains an unreachable device', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockRejectedValue(new TypeError('Failed to fetch')),
  );
  await expect(request('/x')).rejects.toThrow('Could not reach the device');
});

it('rejects a successful response that is not JSON', async () => {
  stubFetch({ ok: true, status: 200 });
  await expect(request('/x')).rejects.toThrow(
    'The device sent an unreadable response',
  );
});
