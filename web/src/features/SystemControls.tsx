import { useState } from 'react';
import { Alert, Button, Stack, Typography } from '@mui/material';
import { rebootDevice } from '../api/system';

export default function SystemControls() {
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState('');
  const [error, setError] = useState('');

  async function reboot() {
    if (
      busy ||
      !window.confirm(
        'Restart the temperature monitor now? Live readings and the web connection will pause briefly.',
      )
    )
      return;
    setBusy(true);
    setMessage('');
    setError('');
    try {
      await rebootDevice();
      setMessage(
        'Restart accepted. Reconnect to the device in about 30 seconds.',
      );
    } catch (cause) {
      setError(
        cause instanceof Error ? cause.message : 'Restart request failed.',
      );
    } finally {
      setBusy(false);
    }
  }

  return (
    <Stack spacing={1.5}>
      <Typography variant="h6">Device restart</Typography>
      <Typography>
        A restart preserves Wi-Fi, administrator, sensor, and history data.
      </Typography>
      <Button variant="outlined" disabled={busy} onClick={() => void reboot()}>
        {busy ? 'Requesting restart…' : 'Restart device'}
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
