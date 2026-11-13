import {
  fireEvent,
  render,
  screen,
  waitFor,
  within,
} from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { factoryResetDevice, rebootDevice } from '../api/system';
import SystemControls from './SystemControls';

vi.mock('../api/system', () => ({
  rebootDevice: vi.fn(),
  factoryResetDevice: vi.fn(),
}));
afterEach(() => {
  vi.clearAllMocks();
});

// Confirmations are dialogs: the action button is named for what it does.
async function confirmInDialog(name: string) {
  const dialog = await screen.findByRole('dialog');
  fireEvent.click(within(dialog).getByRole('button', { name }));
}

it('requires a separate confirmation for each factory-reset history choice', async () => {
  render(<SystemControls />);
  vi.mocked(factoryResetDevice).mockResolvedValue(undefined);
  fireEvent.click(
    screen.getByRole('button', { name: 'Reset settings, preserve history' }),
  );
  // Cancel keeps the settings and history as they are.
  fireEvent.click(
    within(await screen.findByRole('dialog')).getByRole('button', {
      name: 'Cancel',
    }),
  );
  await waitFor(() => expect(screen.queryByRole('dialog')).toBeNull());
  expect(factoryResetDevice).not.toHaveBeenCalled();

  fireEvent.click(
    screen.getByRole('button', { name: 'Reset settings, preserve history' }),
  );
  expect(
    await screen.findByText('Reset settings and preserve history?'),
  ).toBeInTheDocument();
  await confirmInDialog('Reset settings');
  expect(await screen.findByRole('status')).toHaveTextContent(
    /reset accepted/i,
  );
  expect(factoryResetDevice).toHaveBeenLastCalledWith(false);

  fireEvent.click(
    screen.getByRole('button', { name: 'Factory reset and erase history' }),
  );
  expect(
    await screen.findByText(
      /erases Wi-Fi, administrator, sensor settings, and all temperature history/,
    ),
  ).toBeInTheDocument();
  await confirmInDialog('Erase and reset');
  await waitFor(() =>
    expect(factoryResetDevice).toHaveBeenLastCalledWith(true),
  );
});

it('requires confirmation and reports a successful restart request', async () => {
  vi.mocked(rebootDevice).mockResolvedValue(undefined);
  render(<SystemControls />);
  fireEvent.click(screen.getByRole('button', { name: 'Restart device' }));
  expect(rebootDevice).not.toHaveBeenCalled();
  await confirmInDialog('Restart');
  expect(await screen.findByRole('status')).toHaveTextContent(
    /Restart accepted/,
  );
  expect(rebootDevice).toHaveBeenCalledOnce();
});

it('does not restart when declined and reports endpoint errors', async () => {
  render(<SystemControls />);
  fireEvent.click(screen.getByRole('button', { name: 'Restart device' }));
  fireEvent.keyDown(await screen.findByRole('dialog'), { key: 'Escape' });
  await waitFor(() => expect(screen.queryByRole('dialog')).toBeNull());
  expect(rebootDevice).not.toHaveBeenCalled();

  vi.mocked(rebootDevice).mockRejectedValue(new Error('Restart denied'));
  fireEvent.click(screen.getByRole('button', { name: 'Restart device' }));
  await confirmInDialog('Restart');
  expect(await screen.findByText('Restart denied')).toBeInTheDocument();
});
