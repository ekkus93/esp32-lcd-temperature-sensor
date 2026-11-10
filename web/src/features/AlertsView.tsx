import { useEffect, useState } from 'react';
import { Alert, Card, CardContent, Stack, Typography } from '@mui/material';
import { getAlerts, type AlertData } from '../api/alerts';
import { formatDeviceTime } from '../api/time';

// Active alerts are conditions that need attention now; each one clears by itself when the
// condition ends. What happened is on the Events page.
function activeText(item: AlertData['active'][number]) {
  if ('rom_id' in item) return `${item.name}: ${item.state}`;
  if (item.state === 'bus_fault')
    return 'Shared sensor bus fault: probes may read as missing';
  return 'Wi-Fi disconnected: the device has lost its network connection';
}

// When the condition began: the wall-clock time once the clock is trusted, otherwise the time since
// this boot started, as on the Events page.
function since(item: AlertData['active'][number], timezone: string | null) {
  return item.since_utc_seconds === null
    ? `since boot +${(item.since_uptime_ms / 1000).toFixed(0)} seconds`
    : `since ${formatDeviceTime(item.since_utc_seconds * 1000, timezone)}`;
}

export default function AlertsView({ timezone }: { timezone: string | null }) {
  const [active, setActive] = useState<AlertData['active'] | null>(null);
  const [error, setError] = useState('');

  useEffect(() => {
    let live = true;
    const refresh = async () => {
      try {
        // The active list does not depend on the events, so ask for a single event.
        const latest = await getAlerts(undefined, 1);
        if (live) {
          setActive(latest.active);
          setError('');
        }
      } catch (e) {
        if (live)
          setError(e instanceof Error ? e.message : 'Alerts unavailable');
      }
    };
    void refresh();
    const interval = setInterval(() => void refresh(), 5000);
    return () => {
      live = false;
      clearInterval(interval);
    };
  }, []);

  return (
    <Stack spacing={2}>
      {error && <Alert severity="warning">{error}</Alert>}
      <Card>
        <CardContent>
          <Typography variant="h6">Active alerts</Typography>
          <Typography color="text.secondary" variant="body2" sx={{ my: 1 }}>
            Conditions that need attention now. Each one clears by itself when
            the condition ends.
          </Typography>
          {active?.map((item, i) => (
            <Alert
              key={'rom_id' in item ? item.rom_id : `global-${i}`}
              severity="warning"
              sx={{ mt: 1 }}
            >
              {activeText(item)} · {since(item, timezone)}
            </Alert>
          ))}
          {active && !active.length && (
            <Typography>No active alerts.</Typography>
          )}
          {!active && !error && <Typography>Loading alerts…</Typography>}
        </CardContent>
      </Card>
    </Stack>
  );
}
