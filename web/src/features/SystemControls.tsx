import { useState } from 'react';
import { Alert, Button, Stack, Typography } from '@mui/material';
import { factoryResetDevice, rebootDevice } from '../api/system';

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

  async function factoryReset(eraseHistory: boolean) {
    const choice = eraseHistory
      ? 'Factory reset and erase history'
      : 'Reset settings and preserve history';
    const details = eraseHistory
      ? 'This erases Wi-Fi, administrator, sensor settings, and all temperature history.'
      : 'This erases Wi-Fi, administrator, and sensor settings. Temperature history is preserved.';
    if (
      busy ||
      !window.confirm(`${choice}? ${details} The device will restart.`)
    )
      return;
    setBusy(true);
    setMessage('');
    setError('');
    try {
      await factoryResetDevice(eraseHistory);
      setMessage(
        'Factory reset accepted. Reconnect to the device in about 30 seconds.',
      );
    } catch (cause) {
      setError(
        cause instanceof Error
          ? cause.message
          : 'Factory reset request failed.',
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
      <Typography variant="h6">Factory reset</Typography>
      <Typography>
        Both reset choices remove Wi-Fi, administrator, and sensor settings.
        Choose whether to keep recorded temperature history.
      </Typography>
      <Button
        variant="outlined"
        color="warning"
        disabled={busy}
        onClick={() => void factoryReset(false)}
      >
        Reset settings, preserve history
      </Button>
      <Button
        variant="outlined"
        color="error"
        disabled={busy}
        onClick={() => void factoryReset(true)}
      >
        Factory reset and erase history
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
