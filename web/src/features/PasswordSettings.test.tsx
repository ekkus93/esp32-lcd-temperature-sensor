import { fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import PasswordSettings from './PasswordSettings';
afterEach(() => vi.unstubAllGlobals());
function fill(confirm: string) {
  fireEvent.change(screen.getByLabelText(/Current password/), {
    target: { value: 'current-example-password' },
  });
  fireEvent.change(screen.getByLabelText(/^New password/), {
    target: { value: 'new-example-password' },
  });
  fireEvent.change(screen.getByLabelText(/Confirm new password/), {
    target: { value: confirm },
  });
  fireEvent.click(
    screen.getByRole('button', { name: 'Change administrator password' }),
  );
}
it('rejects mismatched confirmation before submitting secrets', async () => {
  vi.stubGlobal('fetch', vi.fn());
  render(<PasswordSettings />);
  fill('different-password');
  await screen.findByText('New passwords do not match.');
  expect(fetch).not.toHaveBeenCalled();
});
it('clears password fields and asks for sign-in after successful rotation', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => ({}) }),
  );
  const expired = vi.fn();
  window.addEventListener('auth-required', expired);
  try {
    render(<PasswordSettings />);
    fill('new-example-password');
    await screen.findByRole('button', {
      name: 'Change administrator password',
    });
    expect(expired).toHaveBeenCalledOnce();
    expect(screen.getByLabelText(/Current password/)).toHaveValue('');
    expect(vi.mocked(fetch).mock.calls[0][0]).toBe('/api/v1/auth/password');
  } finally {
    window.removeEventListener('auth-required', expired);
  }
});
