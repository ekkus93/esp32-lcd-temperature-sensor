import { fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { uploadFirmware } from '../api/auth';
import FirmwareUpdate from './FirmwareUpdate';

vi.mock('../api/auth', () => ({ uploadFirmware: vi.fn() }));
afterEach(() => {
  vi.unstubAllGlobals();
  vi.clearAllMocks();
});

it('confirms and uploads a selected firmware image, then reports restart', async () => {
  vi.stubGlobal(
    'confirm',
    vi.fn(() => true),
  );
  vi.mocked(uploadFirmware).mockResolvedValue(undefined);
  render(<FirmwareUpdate version="0.1.0" partition="ota_0" state="valid" />);
  const image = new File(['image'], 'temperature-monitor.bin', {
    type: 'application/octet-stream',
  });
  fireEvent.change(screen.getByLabelText('Firmware image'), {
    target: { files: [image] },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Install firmware' }));
  expect(window.confirm).toHaveBeenCalledOnce();
  await screen.findByText(/Update accepted/);
  expect(uploadFirmware).toHaveBeenCalledWith(image);
});

it('does not upload when confirmation is declined and reports upload errors', async () => {
  vi.stubGlobal(
    'confirm',
    vi.fn(() => false),
  );
  render(<FirmwareUpdate version="0.1.0" partition="ota_0" state="valid" />);
  const image = new File(['image'], 'image.bin');
  fireEvent.change(screen.getByLabelText('Firmware image'), {
    target: { files: [image] },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Install firmware' }));
  expect(uploadFirmware).not.toHaveBeenCalled();

  vi.mocked(window.confirm).mockReturnValue(true);
  vi.mocked(uploadFirmware).mockRejectedValue(new Error('Image rejected'));
  fireEvent.click(screen.getByRole('button', { name: 'Install firmware' }));
  expect(await screen.findByText('Image rejected')).toBeInTheDocument();
});
