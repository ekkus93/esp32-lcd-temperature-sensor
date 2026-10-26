let csrfToken = '';
export function setCsrf(value: string) {
  csrfToken = value;
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
    const response = await fetch(`/api/v1${path}`, {
      method,
      signal: controller.signal,
      credentials: 'same-origin',
      headers: {
        ...(value !== undefined ? { 'Content-Type': 'application/json' } : {}),
        ...(method !== 'GET' && csrfToken ? { 'X-CSRF-Token': csrfToken } : {}),
        ...extraHeaders,
      },
      body: value === undefined ? undefined : JSON.stringify(value),
    });
    const result = await response.json();
    if (!response.ok) {
      if (response.status === 401 && !path.startsWith('/auth/'))
        window.dispatchEvent(new Event('auth-required'));
      throw new Error(
        typeof result.error === 'string'
          ? result.error
          : `Device request failed (${response.status})`,
      );
    }
    return result as T;
  } finally {
    clearTimeout(timeout);
  }
}
