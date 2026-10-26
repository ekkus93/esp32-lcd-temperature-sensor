import { render, screen } from '@testing-library/react';
import { beforeEach, expect, it, vi } from 'vitest';
import App from './App';
import { empty } from './api/fixtures';
beforeEach(() => {
  vi.stubGlobal('matchMedia', () => ({ matches: false }));
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({ ok: true, json: async () => empty }),
  );
});
it('shows empty sensors honestly without a fabricated reading', async () => {
  render(<App />);
  expect(await screen.findByText('No probes detected')).toBeInTheDocument();
  expect(screen.queryByText('0.0 °C')).not.toBeInTheDocument();
});
it('marks unavailable device data visibly', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockRejectedValue(new Error('Live connection lost')),
  );
  render(<App />);
  expect(await screen.findByText(/Live connection lost/)).toBeInTheDocument();
});
