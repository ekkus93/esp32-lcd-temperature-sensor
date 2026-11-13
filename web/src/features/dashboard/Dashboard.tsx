// The Dashboard (spec §18.4): the live readings panel, the last day as a chart, and the active
// alerts. Limits come from GET /sensors (refreshed every minute) and history from one query for
// all probes (refreshed every five minutes), within the device's public rate limits.
import { useEffect, useMemo, useRef, useState } from 'react';
import {
  Alert,
  Box,
  Button,
  Chip,
  Skeleton,
  ToggleButton,
  ToggleButtonGroup,
  Typography,
} from '@mui/material';
import { useTheme } from '@mui/material/styles';
import CheckCircleOutlineOutlined from '@mui/icons-material/CheckCircleOutlineOutlined';
import type { Snapshot } from '../../api/generated';
import { getSensors } from '../../api/sensors';
import { getHistory, type HistoryResult } from '../../api/history';
import { AlertList } from '../alertItems';
import type { ActiveAlert } from '../alertText';
import TrendChart, { type TrendLimit } from '../chart/TrendChart';
import { timeTicks } from '../chart/scale';
import { chartGaps, chartSeries, pointTime } from '../chart/historySeries';
import LiveReadings from './LiveReadings';
import { formatTemperature, roomState, type PanelProbe } from './roomState';

type Limits = Record<string, { lowC: number | null; highC: number | null }>;
const ranges = [
  { hours: 24, label: '24 hours', title: 'Last 24 hours' },
  { hours: 168, label: '7 days', title: 'Last 7 days' },
  { hours: 720, label: '30 days', title: 'Last 30 days' },
] as const;

function useLimits(): Limits {
  const [limits, setLimits] = useState<Limits>({});
  useEffect(() => {
    let live = true;
    const load = async () => {
      try {
        const sensors = await getSensors();
        if (!live) return;
        setLimits(
          Object.fromEntries(
            sensors.map((s) => [s.rom_id, { lowC: s.low_c, highC: s.high_c }]),
          ),
        );
      } catch {
        // Without limits the gauges show no band; the readings still show.
      }
    };
    void load();
    const interval = setInterval(() => void load(), 60000);
    return () => {
      live = false;
      clearInterval(interval);
    };
  }, []);
  return limits;
}

function updatedText(snapshot: Snapshot, stale: boolean): string {
  if (stale) return 'Live connection lost';
  const seen = snapshot.sensors
    .map((s) => s.last_seen_ms)
    .filter((t): t is number => t !== null);
  if (!seen.length) return 'Waiting for the first reading';
  const seconds = Math.max(
    0,
    Math.floor((snapshot.uptime_ms - Math.max(...seen)) / 1000),
  );
  return seconds === 1
    ? 'Updated 1 second ago'
    : `Updated ${seconds} seconds ago`;
}

const surface = (radius: number) => ({
  bgcolor: 'background.paper',
  borderRadius: `${radius}px`,
  p: { xs: 2, sm: 3 },
  minWidth: 0,
});

interface HistoryEntry {
  context: string; // boot, clock basis and range of the request
  basis: 'utc' | 'uptime';
  bounds: { from: number; to: number }; // the axis the request was made for
  value: HistoryResult | null;
  error: string | null; // the last refresh failed
}
// Which clock a history request uses for this snapshot (as getHistory decides).
const historyBasis = (snapshot: Snapshot): 'utc' | 'uptime' =>
  snapshot.clock.synchronized && snapshot.clock.utc !== null ? 'utc' : 'uptime';

export default function Dashboard({
  snapshot,
  unit,
  timezone,
  stale,
  alerts,
  onOpenEvents,
}: {
  snapshot: Snapshot | null;
  unit: 'C' | 'F';
  timezone: string | null;
  stale: boolean;
  alerts: ActiveAlert[] | null;
  onOpenEvents: () => void;
}) {
  const theme = useTheme();
  const limits = useLimits();
  const [hours, setHours] = useState<number>(24);
  const [hidden, setHidden] = useState<string[]>([]);
  // The last answer and the request it belongs to (review audit F6): it is drawn only for the same
  // boot, clock basis and range, and a failed refresh keeps it with a notice and its own bounds.
  const [history, setHistory] = useState<HistoryEntry | null>(null);
  const [tick, setTick] = useState(0);
  const latest = useRef(snapshot);
  useEffect(() => {
    latest.current = snapshot;
  });

  // One query on entry, on a new range, a reboot or the clock being set, then every five minutes.
  const boot = snapshot?.boot_id;
  const synced = snapshot ? historyBasis(snapshot) === 'utc' : undefined;
  const context = `${boot}:${synced}:${hours}`;
  useEffect(() => {
    const interval = setInterval(() => setTick((n) => n + 1), 300000);
    return () => clearInterval(interval);
  }, []);
  useEffect(() => {
    const current = latest.current;
    if (!current) return;
    // Bounds and basis as requested; an answer for another context is never drawn.
    const basis = historyBasis(current);
    const to =
      basis === 'utc' ? Date.parse(current.clock.utc!) : current.uptime_ms;
    const bounds = { from: Math.max(0, to - hours * 3600000), to };
    let cancelled = false; // a newer request (or leaving) makes this answer obsolete
    getHistory(current, hours)
      .then((value) => {
        if (cancelled) return;
        if (value.time_basis !== basis)
          throw new Error('The device answered for a different clock.');
        setHistory({ context, basis, bounds, value, error: null });
      })
      .catch((cause: unknown) => {
        if (cancelled) return;
        const error =
          cause instanceof Error ? cause.message : 'History unavailable';
        // Same context: keep the chart, marked stale. Otherwise there is nothing to show.
        setHistory((previous) =>
          previous?.context === context && previous.value
            ? { ...previous, error }
            : { context, basis, bounds, value: null, error },
        );
      });
    return () => {
      cancelled = true;
    };
  }, [context, hours, tick]);

  const probes: PanelProbe[] = useMemo(
    () =>
      (snapshot?.sensors ?? [])
        .filter((s) => s.enabled)
        .sort((a, b) => a.order - b.order || a.rom_id.localeCompare(b.rom_id))
        .map((s) => ({
          rom: s.rom_id,
          name: s.name,
          state: s.state,
          celsius: s.temperature_c,
          lowC: limits[s.rom_id]?.lowC ?? null,
          highC: limits[s.rom_id]?.highC ?? null,
        })),
    [snapshot, limits],
  );

  if (!snapshot)
    return (
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: 3 }}>
        <Skeleton
          variant="rounded"
          height={280}
          sx={{ borderRadius: `${theme.corner.extraLarge}px` }}
          aria-label="Loading live readings"
        />
        <Skeleton
          variant="rounded"
          height={360}
          sx={{ borderRadius: '24px' }}
        />
      </Box>
    );

  const state = roomState({
    probes,
    unit,
    stale,
    busFault: snapshot.bus_fault,
    alerts,
    uptimeMs: snapshot.uptime_ms,
  });
  const range = ranges.find((r) => r.hours === hours) ?? ranges[0];
  // Only an answer for this boot, clock basis and range is drawn. A fresh one follows the live
  // clock; one kept after a failed refresh keeps the bounds it was fetched for.
  const entry = history?.context === context ? history : null;
  const result = entry?.value ?? null;
  const basis = entry?.basis ?? historyBasis(snapshot);
  const liveTo =
    basis === 'utc' && snapshot.clock.utc
      ? Date.parse(snapshot.clock.utc)
      : snapshot.uptime_ms;
  const { from, to } =
    entry?.error && entry.value
      ? entry.bounds
      : { from: Math.max(0, liveTo - hours * 3600000), to: liveTo };
  const colour = (order: number) =>
    theme.palette.series[order % theme.palette.series.length];
  const charted = probes.map((p) => {
    const order = snapshot.sensors.find((s) => s.rom_id === p.rom)?.order ?? 0;
    return { ...p, colour: colour(order) };
  });
  const shown = charted.filter((p) => !hidden.includes(p.rom));
  const series = result ? chartSeries(result, shown) : [];
  const gaps = result
    ? chartGaps(
        result,
        shown.map((p) => p.rom),
        timezone,
      )
    : [];
  const limitLines: TrendLimit[] = charted.flatMap((p) => {
    if (hidden.includes(p.rom)) return [];
    const warm = p.state === 'high' && p.highC !== null;
    const cold = p.state === 'low' && p.lowC !== null;
    if (!warm && !cold) return [];
    const tone = warm ? theme.palette.status.high : theme.palette.status.low;
    const celsius = (warm ? p.highC : p.lowC)!;
    return [
      {
        celsius,
        colour: tone.color,
        background: tone.container,
        text: tone.onContainer,
        label: `${p.name} limit ${formatTemperature(celsius, unit)}`,
      },
    ];
  });

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: 3 }}>
      <LiveReadings
        probes={probes}
        state={state}
        unit={unit}
        updated={updatedText(snapshot, stale)}
        live={!stale}
      />
      <Box sx={{ display: 'flex', flexWrap: 'wrap', gap: 3 }}>
        <Box
          component="section"
          aria-labelledby="dashboard-chart-title"
          sx={{
            ...surface(24),
            flex: '2 1 560px',
            display: 'flex',
            flexDirection: 'column',
            gap: 2.5,
          }}
        >
          <Box
            sx={{
              display: 'flex',
              flexWrap: 'wrap',
              alignItems: 'center',
              justifyContent: 'space-between',
              gap: 1.5,
            }}
          >
            <Typography
              id="dashboard-chart-title"
              variant="titleLarge"
              component="h2"
            >
              {range.title}
            </Typography>
            <ToggleButtonGroup
              exclusive
              value={hours}
              onChange={(_, next: number | null) => next && setHours(next)}
              aria-label="Time range"
            >
              {ranges.map((r) => (
                <ToggleButton key={r.hours} value={r.hours}>
                  {r.label}
                </ToggleButton>
              ))}
            </ToggleButtonGroup>
          </Box>
          {charted.length > 0 && (
            <Box
              role="group"
              aria-label="Probes shown"
              sx={{ display: 'flex', flexWrap: 'wrap', gap: 1 }}
            >
              {charted.map((p) => {
                const shown = !hidden.includes(p.rom);
                return (
                  <Chip
                    key={p.rom}
                    clickable
                    variant={shown ? 'filled' : 'outlined'}
                    aria-pressed={shown}
                    onClick={() =>
                      setHidden((current) =>
                        shown
                          ? [...current, p.rom]
                          : current.filter((rom) => rom !== p.rom),
                      )
                    }
                    icon={
                      <Box
                        component="span"
                        aria-hidden="true"
                        sx={{
                          width: 10,
                          height: 10,
                          borderRadius: '50%',
                          bgcolor: p.colour,
                          opacity: shown ? 1 : 0.5,
                          ml: '10px !important',
                        }}
                      />
                    }
                    label={p.name}
                  />
                );
              })}
            </Box>
          )}
          {entry?.error && (
            <Alert
              severity={result ? 'warning' : 'error'}
              role="alert"
              action={
                <Button
                  color="inherit"
                  size="small"
                  onClick={() => setTick((n) => n + 1)}
                >
                  Try again
                </Button>
              }
            >
              {result
                ? `History could not be refreshed (${entry.error}). The chart shows the last answer, up to ${pointTime(entry.bounds.to, basis, timezone)}; live readings above are separate.`
                : `History unavailable: ${entry.error}. It is tried again every five minutes.`}
            </Alert>
          )}
          {result && result.points.length > 0 ? (
            <TrendChart
              series={series}
              from={from}
              to={to}
              ticks={timeTicks(from, to, basis, timezone)}
              unit={unit}
              limits={limitLines}
              gaps={gaps}
              formatTime={(at) => pointTime(at, basis, timezone)}
              label={`${range.title}: five-minute average temperature of ${series.length === 1 ? 'one probe' : `${series.length} probes`}.`}
            />
          ) : (
            <Typography variant="bodyMedium" color="text.secondary">
              {result
                ? 'No history in this range yet. Five-minute averages appear here as they are recorded.'
                : entry?.error
                  ? 'No chart to show.'
                  : 'Loading history…'}
            </Typography>
          )}
        </Box>
        <Box
          component="section"
          aria-labelledby="dashboard-alerts-title"
          sx={{
            ...surface(24),
            flex: '1 1 300px',
            display: 'flex',
            flexDirection: 'column',
            gap: 2,
          }}
        >
          <Box
            sx={{
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'space-between',
              gap: 1.5,
            }}
          >
            <Typography
              id="dashboard-alerts-title"
              variant="titleLarge"
              component="h2"
            >
              Alerts
            </Typography>
            {alerts && alerts.length > 0 && (
              <Typography variant="labelLarge" color="text.secondary">
                {`${alerts.length} active`}
              </Typography>
            )}
          </Box>
          {alerts && alerts.length > 0 && (
            <AlertList items={alerts} timezone={timezone} />
          )}
          {alerts && !alerts.length && (
            <Box
              sx={{
                display: 'flex',
                alignItems: 'center',
                gap: 1.5,
                color: 'text.secondary',
              }}
            >
              <CheckCircleOutlineOutlined
                aria-hidden="true"
                sx={{ color: 'primary.main' }}
              />
              <Typography variant="bodyMedium">No active alerts</Typography>
            </Box>
          )}
          {!alerts && (
            <Typography variant="bodyMedium" color="text.secondary">
              Checking alerts…
            </Typography>
          )}
          <Button
            variant="text"
            onClick={onOpenEvents}
            sx={{ alignSelf: 'flex-start', ml: -1.5, mt: 'auto' }}
          >
            See all events
          </Button>
        </Box>
      </Box>
    </Box>
  );
}
