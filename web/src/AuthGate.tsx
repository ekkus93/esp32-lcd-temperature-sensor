import { useEffect, useState, type ReactNode } from 'react';
import {
  Alert,
  Button,
  Card,
  CardContent,
  Stack,
  TextField,
  Typography,
} from '@mui/material';
import { request, setCsrf } from './api/auth';
export default function AuthGate({ children }: { children: ReactNode }) {
  const [state, setState] = useState<{
    enrolled: boolean;
    csrf?: string;
  } | null>(null);
  const [password, setPassword] = useState('');
  const [secret, setSecret] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  useEffect(() => {
    let active = true;
    request<{ enrolled: boolean; csrf?: string }>('/auth/state')
      .then((next) => {
        if (active) {
          setCsrf(next.csrf ?? '');
          setState(next);
        }
      })
      .catch((e) => {
        if (active)
          setError(e instanceof Error ? e.message : 'Device unavailable');
      });
    const expired = () => {
      setCsrf('');
      setState((old) => ({ enrolled: old?.enrolled ?? true }));
      setError('Session expired. Please sign in.');
    };
    addEventListener('auth-required', expired);
    return () => {
      active = false;
      removeEventListener('auth-required', expired);
    };
  }, []);
  if (state?.csrf) return children;
  async function submit(event: React.FormEvent) {
    event.preventDefault();
    if (!state) return;
    setBusy(true);
    setError('');
    try {
      if (!state.enrolled)
        await request('/auth/enroll', 'POST', {
          setup_secret: secret,
          password,
        });
      const result = await request<{ csrf: string }>('/auth/login', 'POST', {
        password,
      });
      setCsrf(result.csrf);
      setState({ enrolled: true, csrf: result.csrf });
      setPassword('');
      setSecret('');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Authentication failed');
      // An enrollment may have committed before a lost response. Recheck its state.
      if (!state.enrolled) {
        try {
          const current = await request<{ enrolled: boolean; csrf?: string }>(
            '/auth/state',
          );
          if (current.enrolled) {
            setState(current);
            setSecret('');
          }
        } catch {
          /* Preserve the original error until the device is reachable. */
        }
      }
    } finally {
      setBusy(false);
    }
  }
  return (
    <main
      style={{ maxWidth: 520, margin: '8vh auto', fontFamily: 'system-ui' }}
    >
      <Card>
        <CardContent>
          <Typography variant="overline">TEMPERATURE MONITOR</Typography>
          <Typography variant="h4" component="h1">
            {state?.enrolled ? 'Administrator sign in' : 'Set up your monitor'}
          </Typography>
          <Alert severity="info" sx={{ my: 2 }}>
            Local HTTP mode: use your trusted local network. Passwords and
            sessions are not encrypted in transit.
          </Alert>
          {error && (
            <Alert severity="error" role="alert" sx={{ mb: 2 }}>
              {error}
            </Alert>
          )}
          {!state ? (
            <Typography>Connecting to device…</Typography>
          ) : (
            <form onSubmit={submit}>
              <Stack spacing={2}>
                {!state.enrolled && (
                  <>
                    <Typography>
                      Use the device-specific setup secret from the LCD or
                      physical USB setup file.
                    </Typography>
                    <TextField
                      label="One-time setup secret"
                      type="password"
                      autoComplete="one-time-code"
                      required
                      value={secret}
                      onChange={(e) => setSecret(e.target.value)}
                    />
                  </>
                )}
                <TextField
                  label={
                    state.enrolled
                      ? 'Administrator password'
                      : 'Create administrator password'
                  }
                  type="password"
                  autoComplete={
                    state.enrolled ? 'current-password' : 'new-password'
                  }
                  required
                  value={password}
                  onChange={(e) => setPassword(e.target.value)}
                  slotProps={{
                    htmlInput: {
                      minLength: state.enrolled ? 1 : 12,
                      maxLength: 128,
                    },
                  }}
                  helperText={
                    !state.enrolled ? 'At least 12 characters' : undefined
                  }
                />
                <Button variant="contained" type="submit" disabled={busy}>
                  {busy
                    ? 'Please wait…'
                    : state.enrolled
                      ? 'Sign in'
                      : 'Create administrator'}
                </Button>
              </Stack>
            </form>
          )}
        </CardContent>
      </Card>
    </main>
  );
}
