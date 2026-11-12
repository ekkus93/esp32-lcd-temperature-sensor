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
  Tab,
  Tabs,
  ThemeProvider,
  Typography,
} from '@mui/material';
import { createAppTheme } from './theme/theme';
import DashboardOutlined from '@mui/icons-material/DashboardOutlined';
import NotificationsOutlined from '@mui/icons-material/NotificationsOutlined';
import SettingsOutlined from '@mui/icons-material/SettingsOutlined';
import ShowChartOutlined from '@mui/icons-material/ShowChartOutlined';
import ThermostatOutlined from '@mui/icons-material/ThermostatOutlined';
import Navigation, { type Destination } from './shell/Navigation';
import TopBar, { DeviceConditions } from './shell/TopBar';
import { useActiveAlerts } from './shell/useActiveAlerts';
import { temperature } from './api/client';
import { getDisplaySettings } from './api/settings';
import { getProbeScan, type ProbeScanStatus } from './api/probe-scan';
import AuthGate, { AuthProvider } from './AuthGate';
import { useAuth } from './authContext';
import { subscribeLive } from './api/live';
import { delayedError } from './api/delayedError';
import { otaStateText } from './otaState';
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
  'Settings',
] as const;
const routes = ['/', '/history', '/sensors', '/alerts', '/settings'];
const destinations: Destination[] = [
  { label: 'Dashboard', href: '/', icon: <DashboardOutlined /> },
  { label: 'History', href: '/history', icon: <ShowChartOutlined /> },
  { label: 'Sensors', href: '/sensors', icon: <ThermostatOutlined /> },
  { label: 'Alerts', href: '/alerts', icon: <NotificationsOutlined /> },
  { label: 'Settings', href: '/settings', icon: <SettingsOutlined /> },
];
// Events is the second tab of the Alerts page; /events opens it, so older links keep working.
type AlertsTab = 'alerts' | 'events';
const readable = (s: string) => s.replaceAll('_', ' ');
type SettingsTab = 'preferences' | 'wifi' | 'system' | 'password';
const settingsTabs: [SettingsTab, string][] = [
  ['preferences', 'Preferences'],
  ['wifi', 'Wi-Fi'],
  ['system', 'System'],
  ['password', 'Password'],
];
function Shell() {
  const auth = useAuth();
  const [path, setPath] = useState(location.pathname);
  // Settings is split into tabs of related sections. The device's setup page (/setup, served on
  // its own access point) opens on Wi-Fi.
  const [settingsTab, setSettingsTab] = useState<SettingsTab>(
    location.pathname === '/setup' ? 'wifi' : 'preferences',
  );
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
  const activeAlerts = useActiveAlerts();
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
  const theme = useMemo(() => createAppTheme(dark ? 'dark' : 'light'), [dark]);
  useEffect(() => {
    const onPop = () => {
      setPath(location.pathname);
      if (location.pathname === '/setup') setSettingsTab('wifi');
    };
    addEventListener('popstate', onPop);
    return () => removeEventListener('popstate', onPop);
  }, []);
  useEffect(() => {
    // A live-feed problem is shown only once it has lasted ten seconds.
    const problem = delayedError(setError, 10000);
    const unsubscribe = subscribeLive((next) => {
      setSnapshot(next);
      problem.recover();
    }, problem.fail);
    return () => {
      problem.cancel();
      unsubscribe();
    };
  }, []);
  function navigate(index: number) {
    history.pushState({}, '', routes[index]);
    setPath(routes[index]);
  }
  const index =
    path === '/setup'
      ? 4
      : path === '/events'
        ? 3
        : Math.max(0, routes.indexOf(path));
  const alertsTab: AlertsTab = path === '/events' ? 'events' : 'alerts';
  function showAlertsTab(tab: AlertsTab) {
    const next = tab === 'events' ? '/events' : '/alerts';
    history.pushState({}, '', next);
    setPath(next);
  }
  const sensors = snapshot?.sensors ?? [];

  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <div className="app-shell">
        <Navigation
          destinations={destinations}
          current={index}
          onNavigate={navigate}
          badges={{ 3: activeAlerts?.length ?? 0 }}
        />
        <main>
          <TopBar
            title={pages[index]}
            hostname={snapshot?.wifi.mdns_hostname ?? null}
            dark={dark}
            onToggleTheme={() => setDark(!dark)}
            signedIn={Boolean(auth.state?.signed_in)}
            onSignIn={() => navigate(4)}
            onSignOut={() => void auth.signOut()}
          />
          <DeviceConditions snapshot={snapshot} />
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
                        variant="displayMedium"
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
            <Stack spacing={3}>
              <Tabs
                value={alertsTab}
                onChange={(_, tab: AlertsTab) => showAlertsTab(tab)}
                aria-label="Alerts and events"
              >
                <Tab
                  value="alerts"
                  label="Alerts"
                  id="alerts-tab-alerts"
                  aria-controls="alerts-panel"
                />
                <Tab
                  value="events"
                  label="Events"
                  id="alerts-tab-events"
                  aria-controls="alerts-panel"
                />
              </Tabs>
              <div
                role="tabpanel"
                id="alerts-panel"
                aria-labelledby={`alerts-tab-${alertsTab}`}
              >
                <Suspense
                  fallback={
                    <Typography>
                      {alertsTab === 'alerts'
                        ? 'Loading alerts…'
                        : 'Loading events…'}
                    </Typography>
                  }
                >
                  {alertsTab === 'alerts' ? (
                    <AlertsView timezone={timezone} />
                  ) : (
                    <EventsView timezone={timezone} />
                  )}
                </Suspense>
              </div>
            </Stack>
          )}
          {index === 4 && (
            <Stack spacing={3}>
              <Tabs
                value={settingsTab}
                onChange={(_, tab: SettingsTab) => setSettingsTab(tab)}
                // Four short labels: share the width so they all fit on a phone.
                variant="fullWidth"
                sx={{ maxWidth: 560 }}
                aria-label="Settings sections"
              >
                {settingsTabs.map(([value, label]) => (
                  <Tab
                    key={value}
                    value={value}
                    label={label}
                    id={`settings-tab-${value}`}
                    aria-controls={`settings-panel-${value}`}
                    sx={{
                      textTransform: 'none',
                      fontSize: { xs: 14, sm: 15 },
                      minWidth: 0,
                      px: 1,
                      flex: '1 1 auto',
                    }}
                  />
                ))}
              </Tabs>
              <Stack
                spacing={3}
                role="tabpanel"
                id={`settings-panel-${settingsTab}`}
                aria-labelledby={`settings-tab-${settingsTab}`}
              >
                {settingsTab === 'preferences' && (
                  <AuthGate reason="Sign in to change the units, the LCD, the time zone or the network name.">
                    <Suspense
                      fallback={
                        <Typography>Loading device preferences…</Typography>
                      }
                    >
                      <DeviceSettings onApplied={applySettings} />
                    </Suspense>
                  </AuthGate>
                )}
                {settingsTab === 'wifi' && (
                  <AuthGate reason="Sign in to change the Wi-Fi network.">
                    <Suspense
                      fallback={
                        <Typography>Loading network settings…</Typography>
                      }
                    >
                      <NetworkSettings />
                    </Suspense>
                  </AuthGate>
                )}
                {settingsTab === 'system' && (
                  <>
                    <Card>
                      <CardContent>
                        <Typography variant="h6">
                          Storage and diagnostics
                        </Typography>
                        <Typography>
                          Firmware {snapshot?.firmware.version ?? 'unavailable'}{' '}
                          · {snapshot?.firmware.partition ?? 'no active slot'} ·
                          OTA {otaStateText(snapshot?.firmware.ota_state)}
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
                          {snapshot?.logging.healthy
                            ? 'available'
                            : 'unavailable'}
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
                          {snapshot?.logging.interval_seconds ?? 300} seconds.
                          No five-second raw readings are stored.
                        </Typography>
                      </CardContent>
                    </Card>
                    <AuthGate reason="Sign in to update the firmware, restart or reset the device.">
                      <Card>
                        <CardContent>
                          <Suspense
                            fallback={
                              <Typography>Loading firmware updater…</Typography>
                            }
                          >
                            <FirmwareUpdate
                              version={
                                snapshot?.firmware.version ?? 'unavailable'
                              }
                              partition={
                                snapshot?.firmware.partition ?? 'unknown slot'
                              }
                              state={snapshot?.firmware.ota_state ?? 'unknown'}
                            />
                          </Suspense>
                        </CardContent>
                      </Card>
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
                    </AuthGate>
                  </>
                )}
                {settingsTab === 'password' && (
                  <AuthGate reason="Sign in to change the administrator password.">
                    <Suspense
                      fallback={
                        <Typography>Loading password settings…</Typography>
                      }
                    >
                      <PasswordSettings />
                    </Suspense>
                  </AuthGate>
                )}
              </Stack>
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
