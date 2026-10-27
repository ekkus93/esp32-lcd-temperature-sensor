import { useEffect, useMemo, useState } from 'react';
import {
  Alert,
  Button,
  Card,
  CardContent,
  Checkbox,
  FormControlLabel,
  FormGroup,
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
  const [seriesSelection, setSeriesSelection] = useState<{
    key: string;
    roms: string[];
  } | null>(null);
  const [viewportSelection, setViewportSelection] = useState<{
    key: string;
    scale: number;
    offset: number;
  } | null>(null);
  // Load on entry/range/reboot, not every live five-second snapshot.
  const boot = snapshot.boot_id;
  const key = `${boot}:${hours}:${refresh}`;
  const result = loaded?.key === key ? loaded.value : null;
  const error = failure?.key === key ? failure.message : '';
  const exportUrl = historyExportUrl(snapshot, hours);
  const series = useMemo(
    () =>
      [...new Set(result?.points.map((point) => point.rom_id) ?? [])].sort(),
    [result],
  );
  const selectedRoms =
    seriesSelection?.key === key ? seriesSelection.roms : series;
  const viewport =
    viewportSelection?.key === key
      ? viewportSelection
      : { key, scale: 1, offset: 0 };
  const dataFrom = result?.points.length
    ? Math.min(...result.points.map((point) => point.start_ms))
    : 0;
  const dataTo = result?.points.length
    ? Math.max(...result.points.map((point) => point.end_ms))
    : 300000;
  const dataSpan = Math.max(300000, dataTo - dataFrom);
  const windowSpan = dataSpan / viewport.scale;
  const viewTo = dataTo - viewport.offset * dataSpan;
  const viewFrom = viewTo - windowSpan;
  function setViewport(scale: number, offset: number) {
    const boundedScale = Math.min(8, Math.max(1, scale));
    setViewportSelection({
      key,
      scale: boundedScale,
      offset: Math.min(1 - 1 / boundedScale, Math.max(0, offset)),
    });
  }
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
            onClick={() => setViewport(viewport.scale / 2, viewport.offset)}
            disabled={viewport.scale <= 1}
          >
            Zoom out
          </Button>
          <Button
            onClick={() => {
              const nextScale = Math.min(8, viewport.scale * 2);
              const center = viewFrom + windowSpan / 2;
              setViewport(
                nextScale,
                (dataTo - center - dataSpan / nextScale / 2) / dataSpan,
              );
            }}
            disabled={viewport.scale >= 8 || !result?.points.length}
          >
            Zoom in
          </Button>
          <Button
            onClick={() =>
              setViewport(
                viewport.scale,
                viewport.offset + 0.5 / viewport.scale,
              )
            }
            disabled={
              viewport.offset >= 1 - 1 / viewport.scale || viewport.scale <= 1
            }
          >
            Earlier
          </Button>
          <Button
            onClick={() =>
              setViewport(
                viewport.scale,
                viewport.offset - 0.5 / viewport.scale,
              )
            }
            disabled={viewport.offset <= 0 || viewport.scale <= 1}
          >
            Later
          </Button>
          <Button
            onClick={() => setViewport(1, 0)}
            disabled={viewport.scale === 1}
          >
            Reset zoom
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
            {series.length > 0 && (
              <FormGroup row aria-label="History sensor series">
                {series.map((rom) => {
                  const sensor = snapshot.sensors.find(
                    (item) => item.rom_id === rom,
                  );
                  const name =
                    sensor?.name ?? `Probe ${rom.slice(-4).toUpperCase()}`;
                  return (
                    <FormControlLabel
                      key={rom}
                      control={
                        <Checkbox
                          checked={selectedRoms.includes(rom)}
                          onChange={(event) => {
                            const next = event.target.checked
                              ? [...selectedRoms, rom]
                              : selectedRoms.filter((item) => item !== rom);
                            setSeriesSelection({ key, roms: next });
                          }}
                        />
                      }
                      label={`${name} · ${rom}`}
                    />
                  );
                })}
              </FormGroup>
            )}
            {series
              .filter((rom) => selectedRoms.includes(rom))
              .map((rom) => {
                const sensor = snapshot.sensors.find(
                  (item) => item.rom_id === rom,
                );
                const name =
                  sensor?.name ?? `Probe ${rom.slice(-4).toUpperCase()}`;
                const points = result.points.filter(
                  (point) =>
                    point.rom_id === rom &&
                    point.end_ms > viewFrom &&
                    point.start_ms < viewTo,
                );
                if (!points.length) return null;
                const min = Math.min(...points.map((p) => p.minimum_c));
                const max = Math.max(...points.map((p) => p.maximum_c));
                const x = (t: number) =>
                  20 + (560 * (t - viewFrom)) / windowSpan;
                const y = (t: number) =>
                  130 - (110 * (t - min)) / Math.max(1, max - min);
                return (
                  <section key={rom}>
                    <Typography variant="h6" sx={{ mt: 3 }}>
                      {name}
                    </Typography>
                    <Typography>
                      {temperature(min, unit)} to {temperature(max, unit)} ·{' '}
                      {points.length} groups
                    </Typography>
                    <svg
                      viewBox="0 0 600 160"
                      role="img"
                      aria-label={`${name} five-minute average temperature chart`}
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
