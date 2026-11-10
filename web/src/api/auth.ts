import type { AuthState } from './api-auth.generated';
import { authStateValid } from './schema-validators.generated';
export type { AuthState } from './api-auth.generated';
let csrfToken = '';
const validateAuthState = authStateValid;
export function setCsrf(value: string) {
  csrfToken = value;
}
export async function authState(): Promise<AuthState> {
  const value = await request<unknown>('/auth/state');
  if (!validateAuthState(value))
    throw new Error('Invalid authentication state');
  return value;
}
export async function request<T>(
  path: string,
  method = 'GET',
  value?: unknown,
  extraHeaders: Record<string, string> = {},
): Promise<T> {
  const controller = new AbortController();
  // Password rotation performs two slow hashes on the MCU.
  const timeout = setTimeout(
    () => controller.abort(),
    path === '/auth/password' ? 60000 : 30000,
  );
  try {
    let response: Response;
    try {
      response = await fetch(`/api/v1${path}`, {
        method,
        signal: controller.signal,
        credentials: 'same-origin',
        headers: {
          ...(value !== undefined
            ? { 'Content-Type': 'application/json' }
            : {}),
          ...(method !== 'GET' && csrfToken
            ? { 'X-CSRF-Token': csrfToken }
            : {}),
          ...extraHeaders,
        },
        body: value === undefined ? undefined : JSON.stringify(value),
      });
    } catch (e) {
      throw new Error(
        controller.signal.aborted ||
          (e instanceof DOMException && e.name === 'AbortError')
          ? 'The device did not answer in time'
          : 'Could not reach the device',
        { cause: e },
      );
    }
    // The status decides what happens next; the body is only read for a message.
    if (!response.ok) {
      if (response.status === 401 && !path.startsWith('/auth/'))
        window.dispatchEvent(new Event('auth-required'));
      let message = `Device request failed (${response.status})`;
      try {
        const body: unknown = await response.json();
        if (
          typeof body === 'object' &&
          body !== null &&
          'error' in body &&
          typeof body.error === 'string'
        )
          message = body.error;
      } catch {
        // A proxy or error page may not be JSON; the status message stands.
      }
      throw new Error(message);
    }
    try {
      return (await response.json()) as T;
    } catch (e) {
      throw new Error('The device sent an unreadable response', { cause: e });
    }
  } finally {
    clearTimeout(timeout);
  }
}

export async function uploadFirmware(image: File): Promise<void> {
  const maximumBytes = 4 * 1024 * 1024;
  if (!image.size || image.size > maximumBytes)
    throw new Error('Choose a firmware image between 1 byte and 4 MiB.');
  if (!image.name.toLowerCase().endsWith('.bin'))
    throw new Error('Choose an ESP-IDF .bin firmware image.');
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 130000);
  try {
    const response = await fetch('/api/v1/ota', {
      method: 'POST',
      signal: controller.signal,
      credentials: 'same-origin',
      headers: {
        'Content-Type': 'application/octet-stream',
        ...(csrfToken ? { 'X-CSRF-Token': csrfToken } : {}),
      },
      body: image,
    });
    const result: unknown = await response.json();
    if (!response.ok) {
      if (response.status === 401)
        window.dispatchEvent(new Event('auth-required'));
      const message =
        typeof result === 'object' &&
        result !== null &&
        'error' in result &&
        typeof result.error === 'string'
          ? result.error
          : `Firmware upload failed (${response.status})`;
      throw new Error(message);
    }
    if (
      response.status !== 202 ||
      typeof result !== 'object' ||
      result === null ||
      !('status' in result) ||
      result.status !== 'rebooting'
    )
      throw new Error('Unexpected firmware update response');
  } finally {
    clearTimeout(timeout);
  }
}
