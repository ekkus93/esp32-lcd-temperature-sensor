import { useState } from 'react';
import {
  Alert,
  Button,
  Card,
  CardContent,
  Stack,
  TextField,
  Typography,
} from '@mui/material';
import { request, setCsrf } from '../api/auth';
export default function PasswordSettings() {
  const [current, setCurrent] = useState('');
  const [next, setNext] = useState('');
  const [confirm, setConfirm] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  async function submit(event: React.FormEvent) {
    event.preventDefault();
    setError('');
    if (next !== confirm) {
      setError('New passwords do not match.');
      return;
    }
    const bytes = new TextEncoder().encode(next).length;
    if (bytes < 12 || bytes > 128) {
      setError('Use a password between 12 and 128 bytes.');
      return;
    }
    setBusy(true);
    try {
      await request('/auth/password', 'POST', {
        old_password: current,
        new_password: next,
      });
      setCurrent('');
      setNext('');
      setConfirm('');
      setCsrf('');
      window.dispatchEvent(new Event('auth-required'));
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Password change failed');
    } finally {
      setBusy(false);
    }
  }
  return (
    <Card>
      <CardContent>
        <Typography variant="h6">Administrator password</Typography>
        <Typography sx={{ mb: 2 }}>
          Changing your password signs out every browser. Sign in again with the
          new password.
        </Typography>
        {error && (
          <Alert severity="error" role="alert">
            {error}
          </Alert>
        )}
        <form onSubmit={submit}>
          <Stack spacing={2}>
            <TextField
              label="Current password"
              type="password"
              autoComplete="current-password"
              required
              value={current}
              onChange={(e) => setCurrent(e.target.value)}
            />
            <TextField
              label="New password"
              type="password"
              autoComplete="new-password"
              required
              value={next}
              onChange={(e) => setNext(e.target.value)}
              slotProps={{ htmlInput: { minLength: 12, maxLength: 128 } }}
            />
            <TextField
              label="Confirm new password"
              type="password"
              autoComplete="new-password"
              required
              value={confirm}
              onChange={(e) => setConfirm(e.target.value)}
            />
            <Button type="submit" variant="contained" disabled={busy}>
              {busy ? 'Changing password…' : 'Change administrator password'}
            </Button>
          </Stack>
        </form>
      </CardContent>
    </Card>
  );
}
