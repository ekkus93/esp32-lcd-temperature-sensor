import { useState } from 'react';
import { Alert, Button, Stack, Typography } from '@mui/material';
import { factoryResetDevice, rebootDevice } from '../api/system';
import { useConfirm } from '../useConfirm';

export default function SystemControls() {
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState('');
  const [error, setError] = useState('');
  const [confirm, dialog] = useConfirm();

  async function reboot() {
    if (
      busy ||
      !(await confirm({
        title: 'Restart the monitor?',
        message:
          'Live readings and the web connection will pause briefly. Nothing is erased.',
        confirmLabel: 'Restart',
      }))
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
      !(await confirm({
        title: `${choice}?`,
        message: `${details} The device will restart.`,
        confirmLabel: eraseHistory ? 'Erase and reset' : 'Reset settings',
        destructive: true,
      }))
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
      {dialog}
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
