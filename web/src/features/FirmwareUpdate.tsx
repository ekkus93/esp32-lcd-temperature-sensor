import { useState } from 'react';
import { Alert, Button, Stack, Typography } from '@mui/material';
import { uploadFirmware } from '../api/auth';
import { useConfirm } from '../useConfirm';

export default function FirmwareUpdate({
  version,
  partition,
  state,
}: {
  version: string;
  partition: string;
  state: string;
}) {
  const [image, setImage] = useState<File | null>(null);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState('');
  const [error, setError] = useState('');
  const [confirm, dialog] = useConfirm();

  async function submit() {
    if (!image || busy) return;
    setError('');
    setMessage('');
    if (
      !(await confirm({
        title: 'Install this firmware?',
        message:
          'Keep the device powered during upload. It restarts after the image is validated, and settings and history are kept.',
        confirmLabel: 'Install and restart',
        destructive: true,
      }))
    )
      return;
    setBusy(true);
    try {
      await uploadFirmware(image);
      setImage(null);
      setMessage(
        'Update accepted. The device is restarting; reload this page in about a minute.',
      );
    } catch (cause) {
      setError(
        cause instanceof Error ? cause.message : 'Firmware update failed.',
      );
    } finally {
      setBusy(false);
    }
  }

  return (
    <Stack spacing={1.5}>
      {dialog}
      <Typography variant="h6">Firmware update</Typography>
      <Typography>
        Running {version} from {partition} ({state}). Select a firmware .bin
        image no larger than 4 MiB. The device validates it before changing the
        boot slot.
      </Typography>
      <Button component="label" variant="outlined" disabled={busy}>
        {image ? image.name : 'Choose firmware image'}
        <input
          hidden
          aria-label="Firmware image"
          type="file"
          accept=".bin,application/octet-stream"
          onChange={(event) => {
            setImage(event.currentTarget.files?.[0] ?? null);
            setError('');
            setMessage('');
          }}
        />
      </Button>
      <Button
        variant="contained"
        disabled={!image || busy}
        onClick={() => void submit()}
      >
        {busy ? 'Uploading firmware…' : 'Install firmware'}
      </Button>
      {message && (
        <Alert role="status" severity="success">
          {message}
        </Alert>
      )}
      {error && <Alert severity="error">{error}</Alert>}
    </Stack>
  );
}
