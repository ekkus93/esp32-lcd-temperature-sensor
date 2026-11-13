import { useEffect, useRef, useState } from 'react';
import {
  Alert,
  Box,
  Card,
  CardContent,
  Stack,
  Typography,
} from '@mui/material';
import type { ReactNode } from 'react';
import ThermostatOutlined from '@mui/icons-material/ThermostatOutlined';
import WifiOutlined from '@mui/icons-material/WifiOutlined';
import ScheduleOutlined from '@mui/icons-material/ScheduleOutlined';
import RestartAltOutlined from '@mui/icons-material/RestartAltOutlined';
import SdCardAlertOutlined from '@mui/icons-material/SdCardAlertOutlined';
import ScienceOutlined from '@mui/icons-material/ScienceOutlined';
import { getAlerts, type AlertData } from '../api/alerts';
import { formatDeviceTime } from '../api/time';

function wifiState(value: number) {
  return ['offline', 'connecting', 'connected', 'setup'][value] ?? 'unknown';
}

// Events are records of things that happened. Some may need attention, but the active alerts
// page is where a current problem is shown.
function description(event: AlertData['events'][number]) {
  if (event.kind === 'sensor_state')
    return `${event.rom_id ?? 'Shared bus'} · ${event.previous.replaceAll('_', ' ')} → ${event.state.replaceAll('_', ' ')}`;
  if (event.kind === 'wifi_state')
    return `Wi-Fi ${wifiState((event.detail >> 8) & 0xff)} → ${wifiState(event.detail & 0xff)}`;
  if (event.kind === 'clock_sync')
    return `Clock ${event.detail ? 'synchronized' : 'lost synchronization'}`;
  if (event.kind === 'reboot') return 'Device started';
  if (
    event.kind === 'calibration_started' ||
    event.kind === 'calibration_ended'
  )
    return calibrationText(event);
  return 'History storage write failed';
}

// A probe's ice-bath hold (spec 17.3): its start (the detail is the length in minutes), and its end
// (the detail is the reason code).
const calibrationEnds = [
  '',
  'the time ran out',
  'it was ended early',
  'the probe was lost',
] as const;
function calibrationText(event: AlertData['events'][number]) {
  const probe = event.rom_id ?? 'Probe';
  if (event.kind === 'calibration_started')
    return `${probe} · ice-bath hold started for ${event.detail} min`;
  return `${probe} · ice-bath hold ended: ${calibrationEnds[event.detail] ?? 'unknown reason'}`;
}

function kindIcon(kind: AlertData['events'][number]['kind']): ReactNode {
  if (kind === 'sensor_state') return <ThermostatOutlined fontSize="small" />;
  if (kind === 'wifi_state') return <WifiOutlined fontSize="small" />;
  if (kind === 'clock_sync') return <ScheduleOutlined fontSize="small" />;
  if (kind === 'calibration_started' || kind === 'calibration_ended')
    return <ScienceOutlined fontSize="small" />;
  if (kind === 'reboot') return <RestartAltOutlined fontSize="small" />;
  return <SdCardAlertOutlined fontSize="small" />;
}

function timestamp(
  event: AlertData['events'][number],
  timezone: string | null,
) {
  return event.utc_seconds === null
    ? `Boot ${event.boot_id.slice(0, 8)} · +${(event.uptime_ms / 1000).toFixed(0)} seconds`
    : formatDeviceTime(event.utc_seconds * 1000, timezone);
}

export default function EventsView({ timezone }: { timezone: string | null }) {
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
          setError(e instanceof Error ? e.message : 'Events unavailable');
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
          <Typography
            color="text.secondary"
            variant="bodyMedium"
            sx={{ mb: 2 }}
          >
            What happened: device starts, clock and Wi-Fi changes, and probe
            state changes. An event is a record, not an ongoing problem. Events
            are stored in a bounded local journal across reboots.{' '}
            {timezone
              ? `Synchronized times use ${timezone}`
              : 'Device timezone unavailable; synchronized times use UTC'}
            ; unsynchronized times use boot uptime.
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
          {data && data.events.length > 0 && (
            <Box component="ul" aria-label="Events" sx={{ m: 0, p: 0 }}>
              {data.events
                .slice()
                .reverse()
                .map((item, i) => (
                  <Box
                    component="li"
                    key={item.sequence}
                    sx={{
                      listStyle: 'none',
                      display: 'flex',
                      alignItems: 'center',
                      flexWrap: 'wrap',
                      columnGap: 1.5,
                      rowGap: 0.25,
                      minHeight: 56,
                      py: 1,
                      borderTop: i ? 1 : 0,
                      borderColor: 'divider',
                    }}
                  >
                    <Box
                      aria-hidden="true"
                      sx={{ display: 'flex', color: 'text.secondary' }}
                    >
                      {kindIcon(item.kind)}
                    </Box>
                    <Typography
                      variant="bodyLarge"
                      sx={{ flex: '1 1 240px', minWidth: 0 }}
                    >
                      {description(item)}
                    </Typography>
                    <Typography variant="bodySmall" color="text.secondary">
                      {timestamp(item, timezone)}
                    </Typography>
                  </Box>
                ))}
            </Box>
          )}
          {data && !data.events.length && (
            <Typography>No recent events.</Typography>
          )}
          {!data && !error && <Typography>Loading events…</Typography>}
        </CardContent>
      </Card>
    </Stack>
  );
}
