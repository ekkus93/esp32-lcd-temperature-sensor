import { useEffect, useState } from 'react';
import {
  Alert,
  Button,
  Card,
  CardContent,
  Chip,
  Stack,
  Typography,
} from '@mui/material';
import {
  getHistory,
  historyExportUrl,
  type HistoryResult,
} from '../api/history';
import { temperature } from '../api/client';
import type { Snapshot } from '../api/generated';
const ranges = [
  { label: '1h', hours: 1 },
  { label: '24h', hours: 24 },
  { label: '7d', hours: 168 },
  { label: '30d', hours: 720 },
];
export default function HistoryView({
  snapshot,
  unit,
}: {
  snapshot: Snapshot;
  unit: 'C' | 'F';
}) {
  const [hours, setHours] = useState(24);
  const [loaded, setLoaded] = useState<{
    key: string;
    value: HistoryResult;
  } | null>(null);
  const [failure, setFailure] = useState<{
    key: string;
    message: string;
  } | null>(null);
  const [refresh, setRefresh] = useState(0);
  // Load on entry/range/reboot, not every live five-second snapshot.
  const boot = snapshot.boot_id;
  const key = `${boot}:${hours}:${refresh}`;
  const result = loaded?.key === key ? loaded.value : null;
  const error = failure?.key === key ? failure.message : '';
  const exportUrl = historyExportUrl(snapshot, hours);
  useEffect(() => {
    let cancelled = false;
    getHistory(snapshot, hours)
      .then((value) => {
        if (!cancelled) setLoaded({ key, value });
      })
      .catch((e: unknown) => {
        if (!cancelled)
          setFailure({
            key,
            message: e instanceof Error ? e.message : 'History unavailable',
          });
      });
    return () => {
      cancelled = true;
    };
    // A new snapshot only changes the query when the user refreshes.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [boot, hours, refresh, key]);
  return (
    <Card>
      <CardContent>
        <Typography variant="h6">Five-minute temperature history</Typography>
        <Stack direction="row" sx={{ my: 2, gap: 1, flexWrap: 'wrap' }}>
          {ranges.map((range) => (
            <Chip
              key={range.label}
              label={range.label}
              color={hours === range.hours ? 'primary' : 'default'}
              onClick={() => setHours(range.hours)}
            />
          ))}
          <Button onClick={() => setRefresh(refresh + 1)}>
            Refresh history
          </Button>
          <Button
            component="a"
            href={exportUrl}
            disabled={!exportUrl}
            target="_blank"
            rel="noopener"
          >
            Export five-minute CSV
          </Button>
        </Stack>
        {error && <Alert severity="error">{error}</Alert>}
        {!error && !result && <Typography>Loading history…</Typography>}
        {result && (
          <>
            {result.simulated && (
              <Alert severity="info">Simulated history</Alert>
            )}
            {result.truncated && (
              <Alert severity="warning">
                Some overlapping or mixed-boot records were omitted. Narrow the
                range.
              </Alert>
            )}
            <Typography color="text.secondary">
              {result.time_basis === 'uptime'
                ? 'Clock unsynchronized: showing elapsed time for this boot only.'
                : 'Times shown in your browser’s local timezone.'}{' '}
              Missing intervals remain gaps.
            </Typography>
            {!result.points.length && (
              <Typography>No history available in this range.</Typography>
            )}
            {snapshot.sensors.map((sensor) => {
              const points = result.points.filter(
                (point) => point.rom_id === sensor.rom_id,
              );
              if (!points.length) return null;
              const min = Math.min(...points.map((p) => p.minimum_c));
              const max = Math.max(...points.map((p) => p.maximum_c));
              const from = points[0].start_ms;
              const to = Math.max(
                from + 300000,
                ...points.map((p) => p.end_ms),
              );
              const x = (t: number) => 20 + (560 * (t - from)) / (to - from);
              const y = (t: number) =>
                130 - (110 * (t - min)) / Math.max(1, max - min);
              return (
                <section key={sensor.rom_id}>
                  <Typography variant="h6" sx={{ mt: 3 }}>
                    {sensor.name}
                  </Typography>
                  <Typography>
                    {temperature(min, unit)} to {temperature(max, unit)} ·{' '}
                    {points.length} groups
                  </Typography>
                  <svg
                    viewBox="0 0 600 160"
                    role="img"
                    aria-label={`${sensor.name} five-minute average temperature chart`}
                    style={{ width: '100%', maxHeight: 240 }}
                  >
                    {points.map((p, i) => (
                      <g key={`${p.boot_id}:${p.start_ms}`}>
                        <line
                          x1={x(p.start_ms)}
                          x2={x(p.start_ms)}
                          y1={y(p.minimum_c)}
                          y2={y(p.maximum_c)}
                          stroke="currentColor"
                          opacity=".35"
                        />
                        {i > 0 &&
                          !p.partial &&
                          !points[i - 1].partial &&
                          p.boot_id === points[i - 1].boot_id &&
                          p.start_ms === points[i - 1].end_ms && (
                            <line
                              x1={x(points[i - 1].start_ms)}
                              y1={y(points[i - 1].average_c)}
                              x2={x(p.start_ms)}
                              y2={y(p.average_c)}
                              stroke="currentColor"
                            />
                          )}
                        <circle
                          cx={x(p.start_ms)}
                          cy={y(p.average_c)}
                          r="3"
                          fill="currentColor"
                        >
                          <title>
                            {result.time_basis === 'utc'
                              ? new Date(p.start_ms).toLocaleString()
                              : `${Math.floor(p.start_ms / 60000)} min since boot`}
                            : {temperature(p.average_c, unit)}; {p.count}{' '}
                            readings{p.partial ? '; partial' : ''}
                          </title>
                        </circle>
                      </g>
                    ))}
                  </svg>
                </section>
              );
            })}
          </>
        )}
      </CardContent>
    </Card>
  );
}
