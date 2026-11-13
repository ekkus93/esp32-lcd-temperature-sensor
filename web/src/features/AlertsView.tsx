import { useEffect, useState } from 'react';
import {
  Alert,
  Box,
  Card,
  CardContent,
  Stack,
  Typography,
} from '@mui/material';
import CheckCircleOutlineOutlined from '@mui/icons-material/CheckCircleOutlineOutlined';
import { getAlerts, type AlertData } from '../api/alerts';
import { AlertList } from './alertItems';

// Active alerts are conditions that need attention now; each one clears by itself when the
// condition ends. What happened is on the Events tab.
import LoadingBlock from './LoadingBlock';
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
        <CardContent sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
          <Typography variant="bodyMedium" color="text.secondary">
            Conditions that need attention now. Each one clears by itself when
            the condition ends.
          </Typography>
          {active && active.length > 0 && (
            <AlertList items={active} timezone={timezone} />
          )}
          {active && !active.length && (
            <Box
              sx={{
                display: 'flex',
                alignItems: 'center',
                gap: 2,
                py: 3,
                color: 'text.secondary',
              }}
            >
              <CheckCircleOutlineOutlined
                aria-hidden="true"
                sx={{ fontSize: 40, color: 'primary.main' }}
              />
              <Box>
                <Typography
                  variant="titleMedium"
                  component="p"
                  color="text.primary"
                >
                  No active alerts
                </Typography>
                <Typography variant="bodyMedium">
                  Every probe is within its limits and the device is healthy.
                </Typography>
              </Box>
            </Box>
          )}
          {!active && !error && <LoadingBlock label="Loading alerts…" />}
        </CardContent>
      </Card>
    </Stack>
  );
}
