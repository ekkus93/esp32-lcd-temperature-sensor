import { useEffect, useState } from 'react';
import { Alert, Card, CardContent, Stack, Typography } from '@mui/material';
import { request } from '../api/auth';
interface AlertData {
  active: { rom_id?: string; name?: string; state: string }[];
  events: {
    sequence: number;
    rom_id: string | null;
    previous: string;
    state: string;
    uptime_ms: number;
  }[];
}
export default function AlertsView() {
  const [data, setData] = useState<AlertData | null>(null);
  const [error, setError] = useState('');
  useEffect(() => {
    let active = true;
    const refresh = () =>
      request<AlertData>('/alerts')
        .then((next) => {
          if (active) {
            setData(next);
            setError('');
          }
        })
        .catch((e) => {
          if (active)
            setError(e instanceof Error ? e.message : 'Alerts unavailable');
        });
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
              key={item.rom_id ?? `global-${i}`}
              severity="warning"
              sx={{ mt: 1 }}
            >
              {item.name ?? 'Device'}: {item.state.replaceAll('_', ' ')}
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
          <Typography variant="h6">Recent events this boot</Typography>
          {data?.events
            .slice()
            .reverse()
            .map((item) => (
              <Typography key={item.sequence} sx={{ py: 1 }}>
                At {(item.uptime_ms / 1000).toFixed(0)} seconds since boot:{' '}
                {item.rom_id ?? 'Shared bus'} ·{' '}
                {item.previous.replaceAll('_', ' ')} →{' '}
                {item.state.replaceAll('_', ' ')}
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
