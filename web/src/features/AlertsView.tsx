import { useEffect, useRef, useState } from 'react';
import { Alert, Card, CardContent, Stack, Typography } from '@mui/material';
import { getAlerts, type AlertData } from '../api/alerts';
function wifiState(value: number) {
  return ['offline', 'connecting', 'connected', 'setup'][value] ?? 'unknown';
}
function description(event: AlertData['events'][number]) {
  if (event.kind === 'sensor_state')
    return `${event.rom_id ?? 'Shared bus'} · ${event.previous.replaceAll('_', ' ')} → ${event.state.replaceAll('_', ' ')}`;
  if (event.kind === 'wifi_state')
    return `Wi-Fi ${wifiState((event.detail >> 8) & 0xff)} → ${wifiState(event.detail & 0xff)}`;
  if (event.kind === 'clock_sync')
    return `Clock ${event.detail ? 'synchronized' : 'lost synchronization'}`;
  if (event.kind === 'reboot') return 'Device started';
  return 'History storage write failed';
}
function timestamp(event: AlertData['events'][number]) {
  return event.utc_seconds === null
    ? `Boot ${event.boot_id.slice(0, 8)} · +${(event.uptime_ms / 1000).toFixed(0)} seconds`
    : new Date(event.utc_seconds * 1000).toLocaleString();
}
export default function AlertsView() {
  const [data, setData] = useState<AlertData | null>(null);
  const [error, setError] = useState('');
  const [historyGap, setHistoryGap] = useState(false);
  const cursor = useRef(0);
  useEffect(() => {
    let active = true;
    const refresh = async () => {
      try {
        let after = cursor.current;
        let latest: AlertData | undefined;
        const incoming: AlertData['events'] = [];
        for (let page = 0; page < 3; page++) {
          latest = await getAlerts(after || undefined);
          if (
            latest.oldest_available !== null &&
            after > 0 &&
            latest.oldest_available > after + 1
          )
            setHistoryGap(true);
          if (
            after === 0 &&
            latest.oldest_available !== null &&
            latest.oldest_available > 1
          )
            setHistoryGap(true);
          incoming.push(...latest.events);
          if (!latest.has_more || latest.next_after <= after) break;
          after = latest.next_after;
        }
        if (latest && active) {
          cursor.current = Math.max(cursor.current, latest.next_after);
          setData((current) => ({
            ...latest!,
            events: [...(current?.events ?? []), ...incoming]
              .reduce<AlertData['events']>((unique, event) => {
                if (!unique.some((item) => item.sequence === event.sequence))
                  unique.push(event);
                return unique;
              }, [])
              .sort((a, b) => a.sequence - b.sequence)
              .slice(-128),
          }));
          setError('');
        }
      } catch (e) {
        if (active)
          setError(e instanceof Error ? e.message : 'Alerts unavailable');
      }
    };
    void refresh();
    const interval = setInterval(() => void refresh(), 5000);
    return () => {
      active = false;
      clearInterval(interval);
    };
  }, []);
  return (
    <Stack spacing={2}>
      {error && <Alert severity="warning">{error}</Alert>}
      <Card>
        <CardContent>
          <Typography variant="h6">Active alerts</Typography>
          {data?.active.map((item, i) => (
            <Alert
              key={'rom_id' in item ? item.rom_id : `global-${i}`}
              severity="warning"
              sx={{ mt: 1 }}
            >
              {'name' in item ? item.name : 'Device'}:{' '}
              {item.state.replaceAll('_', ' ')}
            </Alert>
          ))}
          {data && !data.active.length && (
            <Typography>No active alerts.</Typography>
          )}
          {!data && !error && <Typography>Loading alerts…</Typography>}
        </CardContent>
      </Card>
      <Card>
        <CardContent>
          <Typography variant="h6">Recent device events</Typography>
          <Typography color="text.secondary" variant="body2" sx={{ my: 1 }}>
            Events are stored in a bounded local journal across reboots. Times
            use local timezone when synchronized, otherwise boot uptime.
          </Typography>
          {data && !data.event_log_healthy && (
            <Alert severity="error" sx={{ mb: 1 }}>
              Durable event history is unavailable. New events may not survive a
              reboot.
            </Alert>
          )}
          {historyGap && (
            <Alert severity="warning" sx={{ mb: 1 }}>
              Some older events have left the bounded event history.
            </Alert>
          )}
          {data?.events
            .slice()
            .reverse()
            .map((item) => (
              <Typography key={item.sequence} sx={{ py: 1 }}>
                {timestamp(item)}: {description(item)}
              </Typography>
            ))}
          {data && !data.events.length && (
            <Typography>No recent events.</Typography>
          )}
        </CardContent>
      </Card>
    </Stack>
  );
}
