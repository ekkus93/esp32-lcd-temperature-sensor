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
  Button,
  Card,
  CardContent,
  Stack,
  TextField,
  Typography,
} from '@mui/material';
import { authState, request, setCsrf } from './api/auth';
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
    async (password: string, ownerCode = '') => {
      if (!state) return;
      setBusy(true);
      setError('');
      try {
        if (!state.enrolled)
          await request('/auth/enroll', 'POST', {
            setup_secret: ownerCode,
            password,
          });
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
  const [secret, setSecret] = useState('');
  if (state?.signed_in) return children;

  async function submit(event: React.FormEvent) {
    event.preventDefault();
    try {
      await signIn(password, secret);
      setPassword('');
      setSecret('');
    } catch {
      // The provider shows the message.
    }
  }
  return (
    <section style={{ maxWidth: 520 }}>
      <Card>
        <CardContent>
          <Typography variant="h5" component="h2">
            {state?.enrolled === false
              ? 'Set up your monitor'
              : 'Administrator sign in'}
          </Typography>
          <Typography color="text.secondary" sx={{ mt: 1 }}>
            {reason} Readings and history do not need a password.
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
                {!state.enrolled && (
                  <>
                    <Typography>
                      Enter the owner code shown on the monitor's screen (or in
                      the physical USB setup file).
                    </Typography>
                    <TextField
                      label="One-time owner code"
                      type="text"
                      autoComplete="one-time-code"
                      required
                      value={secret}
                      onChange={(e) => setSecret(e.target.value)}
                      helperText="16 letters and digits, shown on the screen in two lines. Capitals don't matter and spaces and dashes are fine; there is no 0, 1, I or O."
                      slotProps={{
                        htmlInput: {
                          autoCapitalize: 'characters',
                          autoCorrect: 'off',
                          spellCheck: false,
                        },
                      }}
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
    </section>
  );
}
