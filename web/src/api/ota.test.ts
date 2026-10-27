import { afterEach, expect, it, vi } from 'vitest';
import { setCsrf, uploadFirmware } from './auth';

afterEach(() => {
  vi.unstubAllGlobals();
  setCsrf('');
});

it('sends firmware bytes as an authenticated octet stream and requires reboot acknowledgement', async () => {
  setCsrf('csrf-token');
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      status: 202,
      ok: true,
      json: async () => ({ status: 'rebooting' }),
    }),
  );
  const image = new File(['firmware'], 'monitor.bin');
  await uploadFirmware(image);
  expect(fetch).toHaveBeenCalledWith(
    '/api/v1/ota',
    expect.objectContaining({
      method: 'POST',
      credentials: 'same-origin',
      headers: {
        'Content-Type': 'application/octet-stream',
        'X-CSRF-Token': 'csrf-token',
      },
      body: image,
    }),
  );
});

it('rejects empty, oversized, and non-firmware files before making a request', async () => {
  const fetchMock = vi.fn();
  vi.stubGlobal('fetch', fetchMock);
  await expect(uploadFirmware(new File([], 'empty.bin'))).rejects.toThrow(
    /between 1 byte and 4 MiB/,
  );
  await expect(
    uploadFirmware(
      new File([new Uint8Array(4 * 1024 * 1024 + 1)], 'large.bin'),
    ),
  ).rejects.toThrow(/between 1 byte and 4 MiB/);
  await expect(uploadFirmware(new File(['x'], 'notes.txt'))).rejects.toThrow(
    /\.bin firmware image/,
  );
  expect(fetchMock).not.toHaveBeenCalled();
});

it('surfaces device validation errors', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      status: 422,
      ok: false,
      json: async () => ({ error: 'Firmware image validation failed' }),
    }),
  );
  await expect(
    uploadFirmware(new File(['invalid'], 'invalid.bin')),
  ).rejects.toThrow('Firmware image validation failed');
});
