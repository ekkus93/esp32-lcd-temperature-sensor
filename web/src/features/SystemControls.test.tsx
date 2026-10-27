import { fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { factoryResetDevice, rebootDevice } from '../api/system';
import SystemControls from './SystemControls';

vi.mock('../api/system', () => ({
  rebootDevice: vi.fn(),
  factoryResetDevice: vi.fn(),
}));
afterEach(() => {
  vi.unstubAllGlobals();
  vi.clearAllMocks();
});

it('requires a separate confirmation for each factory-reset history choice', async () => {
  vi.stubGlobal(
    'confirm',
    vi.fn(() => false),
  );
  render(<SystemControls />);
  fireEvent.click(
    screen.getByRole('button', { name: 'Reset settings, preserve history' }),
  );
  expect(factoryResetDevice).not.toHaveBeenCalled();

  vi.mocked(window.confirm).mockReturnValue(true);
  vi.mocked(factoryResetDevice).mockResolvedValue(undefined);
  fireEvent.click(
    screen.getByRole('button', { name: 'Reset settings, preserve history' }),
  );
  expect(await screen.findByRole('status')).toHaveTextContent(
    /reset accepted/i,
  );
  expect(factoryResetDevice).toHaveBeenLastCalledWith(false);

  fireEvent.click(
    screen.getByRole('button', { name: 'Factory reset and erase history' }),
  );
  expect(factoryResetDevice).toHaveBeenLastCalledWith(true);
  expect(window.confirm).toHaveBeenLastCalledWith(
    expect.stringContaining('all temperature history'),
  );
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
