import {
  fireEvent,
  render,
  screen,
  waitFor,
  within,
} from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { uploadFirmware } from '../api/auth';
import FirmwareUpdate from './FirmwareUpdate';

vi.mock('../api/auth', () => ({ uploadFirmware: vi.fn() }));
afterEach(() => {
  vi.clearAllMocks();
});

async function chooseInstall(image: File) {
  fireEvent.change(screen.getByLabelText('Firmware image'), {
    target: { files: [image] },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Install firmware' }));
  return screen.findByRole('dialog');
}

it('confirms in a dialog and uploads the selected image, then reports restart', async () => {
  vi.mocked(uploadFirmware).mockResolvedValue(undefined);
  render(<FirmwareUpdate version="0.1.0" partition="ota_0" state="valid" />);
  const image = new File(['image'], 'temperature-monitor.bin', {
    type: 'application/octet-stream',
  });
  const dialog = await chooseInstall(image);
  expect(dialog).toHaveTextContent('Install this firmware?');
  expect(uploadFirmware).not.toHaveBeenCalled();
  fireEvent.click(
    within(dialog).getByRole('button', { name: 'Install and restart' }),
  );
  await screen.findByText(/Update accepted/);
  expect(uploadFirmware).toHaveBeenCalledWith(image);
});

it('does not upload when the dialog is cancelled, and reports upload errors', async () => {
  render(<FirmwareUpdate version="0.1.0" partition="ota_0" state="valid" />);
  const image = new File(['image'], 'image.bin');
  const dialog = await chooseInstall(image);
  fireEvent.click(within(dialog).getByRole('button', { name: 'Cancel' }));
  await waitFor(() => expect(screen.queryByRole('dialog')).toBeNull());
  expect(uploadFirmware).not.toHaveBeenCalled();

  vi.mocked(uploadFirmware).mockRejectedValue(new Error('Image rejected'));
  fireEvent.click(screen.getByRole('button', { name: 'Install firmware' }));
  fireEvent.click(
    within(await screen.findByRole('dialog')).getByRole('button', {
      name: 'Install and restart',
    }),
  );
  expect(await screen.findByText('Image rejected')).toBeInTheDocument();
});
