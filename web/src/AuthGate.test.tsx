import {
  act,
  fireEvent,
  render,
  screen,
  waitFor,
} from '@testing-library/react';
import { beforeEach, afterEach, expect, it, vi } from 'vitest';
import AuthGate from './AuthGate';
beforeEach(() => vi.stubGlobal('fetch', vi.fn()));
afterEach(() => vi.unstubAllGlobals());
it('gates the app until an administrator signs in', async () => {
  vi.mocked(fetch)
    .mockResolvedValueOnce({
      ok: true,
      json: async () => ({ enrolled: true }),
    } as Response)
    .mockResolvedValueOnce({
      ok: true,
      json: async () => ({ csrf: 'a'.repeat(48) }),
    } as Response);
  render(
    <AuthGate>
      <div>Protected dashboard</div>
    </AuthGate>,
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
    json: async () => ({ enrolled: false }),
  } as Response);
  render(
    <AuthGate>
      <div>Protected dashboard</div>
    </AuthGate>,
  );
  await screen.findByLabelText(/One-time setup secret/);
  expect(
    screen.getByLabelText(/Create administrator password/),
  ).toHaveAttribute('type', 'password');
  expect(screen.queryByText('Protected dashboard')).not.toBeInTheDocument();
});
it('returns to login after session expiration', async () => {
  vi.mocked(fetch).mockResolvedValue({
    ok: true,
    json: async () => ({ enrolled: true, csrf: 'a'.repeat(48) }),
  } as Response);
  render(
    <AuthGate>
      <div>Protected dashboard</div>
    </AuthGate>,
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
    .mockRejectedValueOnce(new Error('Device unavailable'))
    .mockRejectedValueOnce(new Error('Device is still offline'))
    .mockResolvedValueOnce({
      ok: true,
      json: async () => ({ enrolled: true }),
    } as Response);
  render(
    <AuthGate>
      <div>Protected dashboard</div>
    </AuthGate>,
  );
  expect(await screen.findByText('Device unavailable')).toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Retry connection' }));
  expect(
    await screen.findByText('Device is still offline'),
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
    <AuthGate>
      <div>Protected dashboard</div>
    </AuthGate>,
  );
  await screen.findByText('Connecting to device…');
  unmount();
  await act(async () => {
    resolveRequest({
      ok: true,
      json: async () => ({ enrolled: true }),
    } as Response);
  });
  expect(fetch).toHaveBeenCalledTimes(1);
});
