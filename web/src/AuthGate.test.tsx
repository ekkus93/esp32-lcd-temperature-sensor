import {
  act,
  fireEvent,
  render,
  screen,
  waitFor,
} from '@testing-library/react';
import { beforeEach, afterEach, expect, it, vi } from 'vitest';
import AuthGate, { AuthProvider } from './AuthGate';
beforeEach(() => vi.stubGlobal('fetch', vi.fn()));
afterEach(() => vi.unstubAllGlobals());
it('gates the app until an administrator signs in', async () => {
  vi.mocked(fetch)
    .mockResolvedValueOnce({
      ok: true,
      json: async () => ({ enrolled: true, signed_in: false }),
    } as Response)
    .mockResolvedValueOnce({
      ok: true,
      json: async () => ({ csrf: 'a'.repeat(48) }),
    } as Response);
  render(
    <AuthProvider>
      <AuthGate>
        <div>Protected dashboard</div>
      </AuthGate>
    </AuthProvider>,
  );
  expect(screen.queryByText('Protected dashboard')).not.toBeInTheDocument();
  await screen.findByText('Administrator sign in');
  fireEvent.change(screen.getByLabelText(/Administrator password/), {
    target: { value: 'an-example-password' },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));
  await screen.findByText('Protected dashboard');
  expect(fetch).toHaveBeenLastCalledWith(
    '/api/v1/auth/login',
    expect.objectContaining({ credentials: 'same-origin', method: 'POST' }),
  );
});
it('requires a device-specific secret for first enrollment', async () => {
  vi.mocked(fetch).mockResolvedValue({
    ok: true,
    json: async () => ({ enrolled: false, signed_in: false }),
  } as Response);
  render(
    <AuthProvider>
      <AuthGate>
        <div>Protected dashboard</div>
      </AuthGate>
    </AuthProvider>,
  );
  const code = await screen.findByLabelText(/One-time owner code/);
  // The owner code is typed from the LCD, so it is shown (not masked) with a hint about
  // what is accepted; only the password being created is masked.
  expect(code).toHaveAttribute('type', 'text');
  expect(code).toHaveAttribute('autocapitalize', 'characters');
  expect(screen.getByText(/spaces and dashes are fine/i)).toBeInTheDocument();
  expect(
    screen.getByLabelText(/Create administrator password/),
  ).toHaveAttribute('type', 'password');
  expect(screen.queryByText('Protected dashboard')).not.toBeInTheDocument();
});
it('sends the owner code as typed and shows the device message for a malformed one', async () => {
  vi.mocked(fetch)
    .mockResolvedValueOnce({
      ok: true,
      json: async () => ({ enrolled: false, signed_in: false }),
    } as Response)
    .mockResolvedValueOnce({
      ok: false,
      status: 400,
      json: async () => ({
        error:
          'The owner code is 16 characters from A-Z and 2-9 (no 0, 1, I or O); spaces and dashes are fine',
      }),
    } as Response);
  render(
    <AuthProvider>
      <AuthGate>
        <div>Protected dashboard</div>
      </AuthGate>
    </AuthProvider>,
  );
  fireEvent.change(await screen.findByLabelText(/One-time owner code/), {
    target: { value: 'h5rb 2wtd k7m4 q9x0' },
  });
  fireEvent.change(screen.getByLabelText(/Create administrator password/), {
    target: { value: 'correct horse battery' },
  });
  fireEvent.click(
    screen.getByRole('button', { name: /set up|create|continue|save/i }),
  );
  expect(
    await screen.findByText(/16 characters from A-Z and 2-9/),
  ).toBeInTheDocument();
  const enroll = vi
    .mocked(fetch)
    .mock.calls.find(([url]) => String(url).endsWith('/auth/enroll'));
  expect(JSON.parse(String(enroll?.[1]?.body)).setup_secret).toBe(
    'h5rb 2wtd k7m4 q9x0',
  );
});
it('returns to login after session expiration', async () => {
  vi.mocked(fetch).mockResolvedValue({
    ok: true,
    json: async () => ({
      enrolled: true,
      signed_in: true,
      csrf: 'a'.repeat(48),
    }),
  } as Response);
  render(
    <AuthProvider>
      <AuthGate>
        <div>Protected dashboard</div>
      </AuthGate>
    </AuthProvider>,
  );
  await screen.findByText('Protected dashboard');
  fireEvent(window, new Event('auth-required'));
  await waitFor(() =>
    expect(screen.queryByText('Protected dashboard')).not.toBeInTheDocument(),
  );
  expect(
    await screen.findByText('Session expired. Please sign in.'),
  ).toBeInTheDocument();
});

it('allows retrying an initial authentication-state connection failure', async () => {
  vi.mocked(fetch)
    .mockRejectedValueOnce(new TypeError('Failed to fetch'))
    .mockRejectedValueOnce(new TypeError('Failed to fetch'))
    .mockResolvedValueOnce({
      ok: true,
      json: async () => ({ enrolled: true, signed_in: false }),
    } as Response);
  render(
    <AuthProvider>
      <AuthGate>
        <div>Protected dashboard</div>
      </AuthGate>
    </AuthProvider>,
  );
  expect(
    await screen.findByText('Could not reach the device'),
  ).toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Retry connection' }));
  expect(
    await screen.findByText('Could not reach the device'),
  ).toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Retry connection' }));
  expect(await screen.findByRole('button', { name: 'Sign in' })).toBeEnabled();
  expect(fetch).toHaveBeenCalledTimes(3);
  expect(screen.queryByText('Protected dashboard')).not.toBeInTheDocument();
});

it('ignores an authentication response that arrives after unmount', async () => {
  let resolveRequest!: (value: Response) => void;
  vi.mocked(fetch).mockReturnValue(
    new Promise<Response>((resolve) => {
      resolveRequest = resolve;
    }),
  );
  const { unmount } = render(
    <AuthProvider>
      <AuthGate>
        <div>Protected dashboard</div>
      </AuthGate>
    </AuthProvider>,
  );
  await screen.findByText('Connecting to device…');
  unmount();
  await act(async () => {
    resolveRequest({
      ok: true,
      json: async () => ({ enrolled: true, signed_in: false }),
    } as Response);
  });
  expect(fetch).toHaveBeenCalledTimes(1);
});
