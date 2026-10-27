import { fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { rebootDevice } from '../api/system';
import SystemControls from './SystemControls';

vi.mock('../api/system', () => ({ rebootDevice: vi.fn() }));
afterEach(() => {
  vi.unstubAllGlobals();
  vi.clearAllMocks();
});

it('requires confirmation and reports a successful restart request', async () => {
  vi.stubGlobal(
    'confirm',
    vi.fn(() => true),
  );
  vi.mocked(rebootDevice).mockResolvedValue(undefined);
  render(<SystemControls />);
  fireEvent.click(screen.getByRole('button', { name: 'Restart device' }));
  expect(window.confirm).toHaveBeenCalledOnce();
  expect(await screen.findByRole('status')).toHaveTextContent(
    /Restart accepted/,
  );
  expect(rebootDevice).toHaveBeenCalledOnce();
});

it('does not restart when declined and reports endpoint errors', async () => {
  vi.stubGlobal(
    'confirm',
    vi.fn(() => false),
  );
  render(<SystemControls />);
  fireEvent.click(screen.getByRole('button', { name: 'Restart device' }));
  expect(rebootDevice).not.toHaveBeenCalled();

  vi.mocked(window.confirm).mockReturnValue(true);
  vi.mocked(rebootDevice).mockRejectedValue(new Error('Restart denied'));
  fireEvent.click(screen.getByRole('button', { name: 'Restart device' }));
  expect(await screen.findByText('Restart denied')).toBeInTheDocument();
});
