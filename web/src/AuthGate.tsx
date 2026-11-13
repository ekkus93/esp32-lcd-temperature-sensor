import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type ReactNode,
} from 'react';
import {
  Alert,
  Box,
  Button,
  Card,
  CardContent,
  Stack,
  SvgIcon,
  Typography,
} from '@mui/material';
import { authState, request, setCsrf } from './api/auth';
import PasswordField from './PasswordField';
import { AuthContext, useAuth, type AuthState } from './authContext';

export function AuthProvider({ children }: { children: ReactNode }) {
  const [state, setState] = useState<AuthState | null>(null);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [checking, setChecking] = useState(true);
  const mounted = useRef(false);
  const inFlight = useRef(false);

  const refresh = useCallback(async () => {
    if (inFlight.current) return;
    inFlight.current = true;
    setChecking(true);
    try {
      const next = await authState();
      if (mounted.current) {
        setCsrf(next.csrf ?? '');
        setState(next);
        setError('');
      }
    } catch (e) {
      if (mounted.current)
        setError(e instanceof Error ? e.message : 'Device unavailable');
    } finally {
      inFlight.current = false;
      if (mounted.current) setChecking(false);
    }
  }, []);

  useEffect(() => {
    mounted.current = true;
    void Promise.resolve().then(refresh);
    // An owner request that comes back 401 means the session ended.
    const expired = () => {
      setCsrf('');
      setState((old) => ({
        enrolled: old?.enrolled ?? true,
        signed_in: false,
      }));
      setError('Session expired. Please sign in.');
    };
    addEventListener('auth-required', expired);
    return () => {
      mounted.current = false;
      removeEventListener('auth-required', expired);
    };
  }, [refresh]);

  const signIn = useCallback(
    async (password: string) => {
      if (!state) return;
      setBusy(true);
      setError('');
      try {
        if (!state.enrolled)
          await request('/auth/enroll', 'POST', { password });
        const result = await request<{ csrf: string }>('/auth/login', 'POST', {
          password,
        });
        setCsrf(result.csrf);
        setState({ enrolled: true, signed_in: true, csrf: result.csrf });
      } catch (e) {
        setError(e instanceof Error ? e.message : 'Authentication failed');
        // An enrollment may have committed before a lost response. Recheck its state.
        if (!state.enrolled) {
          try {
            const current = await authState();
            if (current.enrolled) setState(current);
          } catch {
            /* Preserve the original error until the device is reachable. */
          }
        }
        throw e;
      } finally {
        setBusy(false);
      }
    },
    [state],
  );

  const signOut = useCallback(async () => {
    try {
      await request('/auth/logout', 'POST');
    } catch {
      // The session may already be gone; either way this browser is signed out below.
    }
    setCsrf('');
    setState((old) => ({ enrolled: old?.enrolled ?? true, signed_in: false }));
    setError('');
  }, []);

  const value = useMemo(
    () => ({ state, error, checking, busy, refresh, signIn, signOut }),
    [state, error, checking, busy, refresh, signIn, signOut],
  );
  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

// Shows `children` to a signed-in owner and an inline sign-in (or first-time setup) otherwise.
export default function AuthGate({
  children,
  reason = 'Sign in to change settings.',
}: {
  children: ReactNode;
  reason?: string;
}) {
  const { state, error, checking, busy, refresh, signIn } = useAuth();
  const [password, setPassword] = useState('');
  if (state?.signed_in) return children;

  async function submit(event: React.FormEvent) {
    event.preventDefault();
    try {
      await signIn(password);
      setPassword('');
    } catch {
      // The provider shows the message.
    }
  }
  return (
    <Box
      component="section"
      sx={{ display: 'flex', justifyContent: 'center', py: { xs: 2, sm: 6 } }}
    >
      <Card sx={{ width: '100%', maxWidth: 480, borderRadius: '28px' }}>
        <CardContent sx={{ p: { xs: 3, sm: 4 } }}>
          <Box
            aria-hidden="true"
            sx={{
              width: 56,
              height: 56,
              borderRadius: '18px',
              bgcolor: 'var(--app-panel-background)',
              color: 'var(--app-panel-text)',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              mb: 2.5,
            }}
          >
            <SvgIcon sx={{ fontSize: 30 }}>
              <path
                d="M5 19c0-8 6-14 15-14 0 9-6 15-14 15M5 19l8-8"
                fill="none"
                stroke="currentColor"
                strokeWidth="1.8"
                strokeLinecap="round"
                strokeLinejoin="round"
              />
            </SvgIcon>
          </Box>
          <Typography variant="headlineSmall" component="h2">
            {state?.enrolled === false
              ? 'Set up your monitor'
              : 'Administrator sign in'}
          </Typography>
          <Typography
            color="text.secondary"
            variant="bodyMedium"
            sx={{ mt: 1 }}
          >
            {reason} Readings and history do not need a password.
          </Typography>
          <Alert severity="info" sx={{ my: 2.5 }}>
            Local HTTP mode: use your trusted local network. Passwords and
            sessions are not encrypted in transit.
          </Alert>
          {error && (
            <Alert severity="error" role="alert" sx={{ mb: 2 }}>
              {error}
            </Alert>
          )}
          {!state ? (
            <Stack spacing={2}>
              <Typography>
                {checking
                  ? 'Connecting to device…'
                  : 'Device connection unavailable.'}
              </Typography>
              <Button
                variant="contained"
                disabled={checking}
                onClick={() => void refresh()}
              >
                {checking ? 'Connecting…' : 'Retry connection'}
              </Button>
            </Stack>
          ) : (
            <form onSubmit={submit}>
              <Stack spacing={2}>
                <PasswordField
                  label={
                    state.enrolled
                      ? 'Administrator password'
                      : 'Create administrator password'
                  }
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
                {busy && (
                  <Typography role="status" color="text.secondary">
                    {state.enrolled
                      ? 'Logging in… the device is checking the password. Please wait.'
                      : 'Creating the administrator password… the device is saving it. Please wait.'}
                  </Typography>
                )}
                <Button variant="contained" type="submit" disabled={busy}>
                  {busy
                    ? state.enrolled
                      ? 'Logging in…'
                      : 'Creating…'
                    : state.enrolled
                      ? 'Sign in'
                      : 'Create administrator'}
                </Button>
              </Stack>
            </form>
          )}
        </CardContent>
      </Card>
    </Box>
  );
}
