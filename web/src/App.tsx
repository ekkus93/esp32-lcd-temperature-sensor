import {
  lazy,
  Suspense,
  useCallback,
  useEffect,
  useMemo,
  useState,
} from 'react';
import {
  Alert,
  Button,
  Card,
  CardContent,
  Chip,
  CssBaseline,
  Stack,
  ThemeProvider,
  Typography,
  createTheme,
} from '@mui/material';
import { temperature } from './api/client';
import { getDisplaySettings } from './api/settings';
import { getProbeScan, type ProbeScanStatus } from './api/probe-scan';
import AuthGate, { AuthProvider } from './AuthGate';
import { useAuth } from './authContext';
import { subscribeLive } from './api/live';
import type { Snapshot } from './api/generated';
const NetworkSettings = lazy(() => import('./features/NetworkSettings'));
const SensorsView = lazy(() => import('./features/SensorsView'));
const AlertsView = lazy(() => import('./features/AlertsView'));
const EventsView = lazy(() => import('./features/EventsView'));
const HistoryView = lazy(() => import('./features/HistoryView'));
const PasswordSettings = lazy(() => import('./features/PasswordSettings'));
const DeviceSettings = lazy(() => import('./features/DeviceSettings'));
const FirmwareUpdate = lazy(() => import('./features/FirmwareUpdate'));
const SystemControls = lazy(() => import('./features/SystemControls'));
const pages = [
  'Dashboard',
  'History',
  'Sensors',
  'Alerts',
  'Events',
  'Settings',
] as const;
const routes = ['/', '/history', '/sensors', '/alerts', '/events', '/settings'];
const readable = (s: string) => s.replaceAll('_', ' ');
function Shell() {
  const auth = useAuth();
  const [path, setPath] = useState(location.pathname);
  const [snapshot, setSnapshot] = useState<Snapshot | null>(null);
  const [error, setError] = useState('Connecting to device…');
  const [dark, setDarkState] = useState(() => {
    // A choice made with the toggle is remembered; otherwise follow the system setting.
    try {
      const saved = localStorage.getItem('theme');
      if (saved === 'dark' || saved === 'light') return saved === 'dark';
    } catch {
      // Storage can be blocked (private windows); the toggle still works for this visit.
    }
    return matchMedia?.('(prefers-color-scheme: dark)').matches ?? false;
  });
  const setDark = (next: boolean) => {
    setDarkState(next);
    try {
      localStorage.setItem('theme', next ? 'dark' : 'light');
    } catch {
      // Not remembered, still applied.
    }
  };
  const [unit, setUnit] = useState<'C' | 'F'>('F');
  const [timezone, setTimezone] = useState<string | null>(null);
  const [probeScan, setProbeScan] = useState<ProbeScanStatus | null>(null);
  const applySettings = useCallback(
    (nextUnit: 'C' | 'F', nextTimezone: string) => {
      setUnit(nextUnit);
      setTimezone(nextTimezone);
    },
    [],
  );
  const [settingsFailed, setSettingsFailed] = useState(false);
  useEffect(() => {
    let active = true;
    let timer: ReturnType<typeof setTimeout> | undefined;
    let attempt = 0;
    const load = () => {
      getDisplaySettings()
        .then((settings) => {
          if (!active) return;
          applySettings(settings.units, settings.timezone);
          setSettingsFailed(false);
        })
        .catch(() => {
          if (!active) return;
          // Keep the default unit on screen, say so, and try again with a growing delay.
          setSettingsFailed(true);
          timer = setTimeout(load, Math.min(30000, 2000 * 2 ** attempt++));
        });
    };
    load();
    return () => {
      active = false;
      clearTimeout(timer);
    };
  }, [applySettings]);
  useEffect(() => {
    let active = true;
    let inFlight = false;
    const refresh = async () => {
      if (inFlight) return;
      inFlight = true;
      try {
        const next = await getProbeScan();
        if (active) setProbeScan(next);
      } catch {
        if (active) setProbeScan(null);
      } finally {
        inFlight = false;
      }
    };
    void Promise.resolve().then(refresh);
    const interval = setInterval(() => void refresh(), 5000);
    return () => {
      active = false;
      clearInterval(interval);
    };
  }, []);
  const theme = useMemo(
    () =>
      createTheme({
        palette: {
          mode: dark ? 'dark' : 'light',
          primary: { main: dark ? '#a1d0bf' : '#22534a' },
          warning: {
            main: dark ? '#ffb74d' : '#a64b00',
            contrastText: dark ? '#1b1305' : '#ffffff',
          },
          background: { default: dark ? '#111815' : '#f5f8f5' },
        },
        shape: { borderRadius: 20 },
        typography: { fontFamily: 'system-ui, sans-serif' },
        components: {
          MuiButton: {
            styleOverrides: {
              root: { textTransform: 'none', borderRadius: 24 },
            },
          },
          MuiCard: {
            styleOverrides: {
              root: {
                boxShadow: 'none',
                border: '1px solid',
                borderColor: dark ? '#394b43' : '#d8e4dc',
              },
            },
          },
        },
      }),
    [dark],
  );
  useEffect(() => {
    const onPop = () => setPath(location.pathname);
    addEventListener('popstate', onPop);
    return () => removeEventListener('popstate', onPop);
  }, []);
  useEffect(() => {
    return subscribeLive((next) => {
      setSnapshot(next);
      setError('');
    }, setError);
  }, []);
  function navigate(index: number) {
    history.pushState({}, '', routes[index]);
    setPath(routes[index]);
  }
  const index = path === '/setup' ? 5 : Math.max(0, routes.indexOf(path));
  const sensors = snapshot?.sensors ?? [];

  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <div className="app-shell">
        <nav
          aria-label="Main navigation"
          style={{ background: theme.palette.background.paper }}
        >
          <div className="brand">
            Temperature
            <br />
            <strong>Monitor</strong>
          </div>
          {pages.map((page, i) => (
            <Button
              key={page}
              variant={i === index ? 'contained' : 'text'}
              onClick={() => navigate(i)}
              aria-current={i === index ? 'page' : undefined}
            >
              {page}
            </Button>
          ))}
        </nav>
        <main>
          <header>
            <div>
              <Typography variant="overline">
                LOCAL TEMPERATURE MONITOR
              </Typography>
              <Typography variant="h4" component="h1">
                {pages[index]}
              </Typography>
            </div>
            <Stack direction="row" sx={{ gap: 1 }}>
              {auth.state?.signed_in ? (
                <Button onClick={() => void auth.signOut()}>Sign out</Button>
              ) : (
                <Button onClick={() => navigate(5)}>Sign in</Button>
              )}
              <Button
                onClick={() => setDark(!dark)}
                aria-label="Toggle dark theme"
              >
                {dark ? 'Light' : 'Dark'} theme
              </Button>
            </Stack>
          </header>
          <Stack direction="row" sx={{ my: 3, gap: 1, flexWrap: 'wrap' }}>
            <Chip label={`Wi-Fi: ${snapshot?.wifi.state ?? 'unknown'}`} />
            <Chip
              label={
                snapshot?.clock.synchronized
                  ? 'Clock synchronized'
                  : 'Time not synchronized'
              }
            />
            <Chip
              label={
                snapshot?.logging.healthy
                  ? 'Logging healthy · 5 min'
                  : 'History logging unavailable'
              }
            />
            {snapshot?.simulated && <Chip label="SIMULATION" color="warning" />}
          </Stack>
          {settingsFailed && (
            <Alert severity="info" sx={{ mb: 2 }} role="status">
              Display settings could not be loaded, so temperatures are shown in
              °F and times in the device clock until they load. Retrying…
            </Alert>
          )}
          {error && (
            <Alert severity="warning" sx={{ mb: 2 }} role="status">
              {error} {snapshot && 'Cached readings are stale.'}
            </Alert>
          )}
          {snapshot?.bus_fault && (
            <Alert severity="error" sx={{ mb: 2 }}>
              Shared sensor bus fault. Current readings unavailable.
            </Alert>
          )}
          {probeScan?.enabled && probeScan.overflow && (
            <Alert severity="warning" sx={{ mb: 2 }}>
              More than five probes were detected (
              {probeScan.complete
                ? probeScan.discovered_count
                : `at least ${probeScan.discovered_count}`}
              ). Managed probes keep reading priority; remove excess probes to
              monitor them.
            </Alert>
          )}
          {probeScan?.enabled && !probeScan.complete && (
            <Alert severity="warning" sx={{ mb: 2 }}>
              Probe discovery is incomplete. Previously selected probes remain
              prioritized until a complete scan succeeds.
            </Alert>
          )}
          {index === 0 && (
            <>
              <div className="sensor-grid">
                {sensors.map((s) => (
                  <Card key={s.rom_id}>
                    <CardContent>
                      <Typography variant="subtitle1">{s.name}</Typography>
                      <Typography
                        className="reading"
                        variant="h3"
                        sx={{ opacity: error ? 0.5 : 1 }}
                      >
                        {temperature(s.temperature_c, unit)}
                      </Typography>
                      <Chip
                        size="small"
                        label={error ? 'Stale' : readable(s.state)}
                        color={
                          ['high', 'low', 'disconnected'].includes(s.state)
                            ? 'warning'
                            : 'default'
                        }
                      />
                      <Typography
                        variant="caption"
                        component="p"
                        sx={{ mt: 2 }}
                      >
                        ROM {s.rom_id}
                      </Typography>
                      <Typography variant="caption" color="text.secondary">
                        {s.last_seen_ms === null
                          ? 'No valid reading yet'
                          : `Updated ${Math.floor(Math.max(0, snapshot!.uptime_ms - s.last_seen_ms) / 1000)} seconds ago`}
                      </Typography>
                    </CardContent>
                  </Card>
                ))}
              </div>
              {!sensors.length && (
                <Card>
                  <CardContent>
                    <Typography variant="h6">No probes detected</Typography>
                    <Typography color="text.secondary">
                      Monitoring supports up to five probes. No alarms are
                      generated for empty positions.
                    </Typography>
                  </CardContent>
                </Card>
              )}
              <Card sx={{ mt: 3 }}>
                <CardContent>
                  <Typography variant="h6">Temperature history</Typography>
                  <Typography color="text.secondary">
                    Explore five-minute averages, minima and maxima in the
                    history view.
                  </Typography>
                  <Button onClick={() => navigate(1)}>Open history</Button>
                </CardContent>
              </Card>
            </>
          )}
          {index === 1 && snapshot && (
            <Suspense fallback={<Typography>Loading history…</Typography>}>
              <HistoryView
                snapshot={snapshot}
                unit={unit}
                timezone={timezone}
              />
            </Suspense>
          )}
          {index === 2 && (
            <AuthGate reason="Sign in to view and change sensor settings.">
              <Suspense fallback={<Typography>Loading sensors…</Typography>}>
                <SensorsView unit={unit} />
              </Suspense>
            </AuthGate>
          )}
          {index === 3 && (
            <Suspense fallback={<Typography>Loading alerts…</Typography>}>
              <AlertsView timezone={timezone} />
            </Suspense>
          )}
          {index === 4 && (
            <Suspense fallback={<Typography>Loading events…</Typography>}>
              <EventsView timezone={timezone} />
            </Suspense>
          )}
          {index === 5 && (
            <Stack spacing={3}>
              <Card>
                <CardContent>
                  <Typography variant="h6">Storage and diagnostics</Typography>
                  <Typography>
                    Firmware {snapshot?.firmware.version ?? 'unavailable'} ·{' '}
                    {snapshot?.firmware.partition ?? 'no active slot'} · OTA{' '}
                    {snapshot?.firmware.ota_state ?? 'unknown'}
                  </Typography>
                  <Typography>
                    {snapshot?.wifi.mdns_hostname ? (
                      <>
                        Network name:{' '}
                        <strong>{`http://${snapshot.wifi.mdns_hostname}.local`}</strong>
                      </>
                    ) : (
                      'Network name unavailable: use the device address.'
                    )}
                  </Typography>
                  <Typography>
                    Five-minute history:{' '}
                    {snapshot?.logging.healthy ? 'available' : 'unavailable'}
                  </Typography>
                  {snapshot?.logging.storage_available ? (
                    <Typography color="text.secondary">
                      {(
                        snapshot.logging.storage_used_bytes /
                        1024 /
                        1024
                      ).toFixed(2)}{' '}
                      MiB used of{' '}
                      {(
                        snapshot.logging.storage_total_bytes /
                        1024 /
                        1024
                      ).toFixed(2)}{' '}
                      MiB
                    </Typography>
                  ) : (
                    <Typography color="text.secondary">
                      Storage capacity is unavailable until the history
                      filesystem mounts.
                    </Typography>
                  )}
                  <Typography color="text.secondary">
                    Aggregates are recorded every{' '}
                    {snapshot?.logging.interval_seconds ?? 300} seconds. No
                    five-second raw readings are stored.
                  </Typography>
                </CardContent>
              </Card>
              <AuthGate reason="Sign in to change device settings, update firmware or manage the network.">
                <Card>
                  <CardContent>
                    <Suspense
                      fallback={
                        <Typography>Loading system controls…</Typography>
                      }
                    >
                      <SystemControls />
                    </Suspense>
                  </CardContent>
                </Card>
                <Suspense
                  fallback={
                    <Typography>Loading device preferences…</Typography>
                  }
                >
                  <DeviceSettings onApplied={applySettings} />
                </Suspense>
                <Card>
                  <CardContent>
                    <Suspense
                      fallback={
                        <Typography>Loading firmware updater…</Typography>
                      }
                    >
                      <FirmwareUpdate
                        version={snapshot?.firmware.version ?? 'unavailable'}
                        partition={
                          snapshot?.firmware.partition ?? 'unknown slot'
                        }
                        state={snapshot?.firmware.ota_state ?? 'unknown'}
                      />
                    </Suspense>
                  </CardContent>
                </Card>
                <Suspense
                  fallback={<Typography>Loading network settings…</Typography>}
                >
                  <NetworkSettings />
                  <PasswordSettings />
                </Suspense>
              </AuthGate>
            </Stack>
          )}
        </main>
      </div>
    </ThemeProvider>
  );
}

export default function App() {
  return (
    <AuthProvider>
      <Shell />
    </AuthProvider>
  );
}
