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
import { formatDeviceTime } from '../api/time';
import type { Snapshot } from '../api/generated';
const ranges = [
  { label: '1h', hours: 1 },
  { label: '24h', hours: 24 },
  { label: '7d', hours: 168 },
  { label: '30d', hours: 720 },
];
function minutes(ms: number) {
  return `${Math.round(ms / 60000)} min`;
}
// A readable description of a missing interval, in the same basis as the chart.
function gapText(
  start: number,
  end: number,
  basis: string,
  timezone: string | null,
) {
  const missing = `${minutes(end - start)} missing`;
  return basis === 'utc'
    ? `${formatDeviceTime(start, timezone)} to ${formatDeviceTime(end, timezone)} (${missing})`
    : `${minutes(start)} to ${minutes(end)} since boot (${missing})`;
}
// The LCD Trends graph colours probes by position (firmware display_view.cpp line_colors), so the
// same probe has the same line colour on the device and in the browser.
const probeColours = ['#7fc8ff', '#ffc857', '#ff8a80', '#c3a6ff', '#7fe0b0'];

export default function HistoryView({
  snapshot,
  unit,
  timezone,
}: {
  snapshot: Snapshot;
  unit: 'C' | 'F';
  timezone: string | null;
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
  // The clock syncing changes the time basis of the answer (elapsed vs. UTC), so it reloads too.
  const synced = snapshot.clock.synchronized;
  const key = `${boot}:${synced}:${hours}:${refresh}`;
  const result = loaded?.key === key ? loaded.value : null;
  const error = failure?.key === key ? failure.message : '';
  const exportUrl = historyExportUrl(snapshot, hours);
  // Probes are listed in their position order (Sensor 1, 2, ...), as on the LCD. A probe no longer
  // in the live snapshot has no position, so it follows the others, ordered by its ROM ID.
  const series = useMemo(() => {
    const position = (rom: string) =>
      snapshot.sensors.find((sensor) => sensor.rom_id === rom)?.order ??
      Number.MAX_SAFE_INTEGER;
    return [...new Set(result?.points.map((point) => point.rom_id) ?? [])].sort(
      (a, b) => position(a) - position(b) || a.localeCompare(b),
    );
  }, [result, snapshot]);
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
  }, [boot, synced, hours, refresh, key]);
  // Every selected probe with data in the zoomed window is one line of the shared chart.
  const chartSeries = series
    .filter((rom) => selectedRoms.includes(rom))
    .map((rom) => {
      const sensor = snapshot.sensors.find((item) => item.rom_id === rom);
      return {
        rom,
        name: sensor?.name ?? `Probe ${rom.slice(-4).toUpperCase()}`,
        colour: probeColours[(sensor?.order ?? 0) % probeColours.length],
        points:
          result?.points.filter(
            (point) =>
              point.rom_id === rom &&
              point.end_ms > viewFrom &&
              point.start_ms < viewTo,
          ) ?? [],
      };
    })
    .filter((item) => item.points.length > 0);
  const chartValues = chartSeries.flatMap((item) => item.points);
  const chartMin = chartValues.length
    ? Math.min(...chartValues.map((p) => p.minimum_c))
    : 0;
  const chartMax = chartValues.length
    ? Math.max(...chartValues.map((p) => p.maximum_c))
    : 1;
  const x = (t: number) => 56 + (532 * (t - viewFrom)) / windowSpan;
  const y = (t: number) =>
    140 - (126 * (t - chartMin)) / Math.max(1, chartMax - chartMin);

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
              aria-pressed={hours === range.hours}
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
                : timezone
                  ? `Times shown in ${timezone}.`
                  : 'Device timezone unavailable; synchronized times shown in UTC.'}{' '}
              Missing intervals remain gaps.
            </Typography>
            {!result.points.length && (
              <Typography>No history available in this range.</Typography>
            )}
            {result.points.length > 0 &&
              viewport.scale > 1 &&
              !result.points.some(
                (point) =>
                  selectedRoms.includes(point.rom_id) &&
                  point.end_ms > viewFrom &&
                  point.start_ms < viewTo,
              ) && (
                <Typography>
                  No data in this zoomed view. Zoom out, or use Earlier and
                  Later.
                </Typography>
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
                      label={
                        <span>
                          <span
                            aria-hidden="true"
                            style={{
                              display: 'inline-block',
                              width: 12,
                              height: 12,
                              marginRight: 6,
                              background:
                                probeColours[
                                  (sensor?.order ?? 0) % probeColours.length
                                ],
                            }}
                          />
                          {`${name} · ${rom}`}
                        </span>
                      }
                    />
                  );
                })}
              </FormGroup>
            )}
            {chartSeries.length > 0 && (
              <>
                <svg
                  viewBox="0 0 600 170"
                  role="img"
                  aria-label={`Five-minute average temperature chart for ${chartSeries.length} ${chartSeries.length === 1 ? 'probe' : 'probes'}`}
                  style={{ width: '100%', maxHeight: 300, marginTop: 16 }}
                >
                  <text
                    x="50"
                    y="14"
                    textAnchor="end"
                    fontSize="11"
                    fill="currentColor"
                  >
                    {temperature(chartMax, unit)}
                  </text>
                  <text
                    x="50"
                    y="146"
                    textAnchor="end"
                    fontSize="11"
                    fill="currentColor"
                  >
                    {temperature(chartMin, unit)}
                  </text>
                  <line
                    x1="56"
                    y1="10"
                    x2="56"
                    y2="140"
                    stroke="currentColor"
                    opacity=".4"
                  />
                  <line
                    x1="56"
                    y1="140"
                    x2="588"
                    y2="140"
                    stroke="currentColor"
                    opacity=".4"
                  />
                  {(() => {
                    // Missing intervals are shaded once, whichever probe they belong to.
                    const seen = new Set<string>();
                    return result?.gaps
                      .filter(
                        (gap) =>
                          chartSeries.some(
                            (item) =>
                              item.rom === gap.rom_id &&
                              item.points.some(
                                (point) => point.boot_id === gap.boot_id,
                              ),
                          ) &&
                          gap.end_ms > viewFrom &&
                          gap.start_ms < viewTo,
                      )
                      .filter((gap) => {
                        const key = `${gap.boot_id}:${gap.start_ms}:${gap.end_ms}`;
                        if (seen.has(key)) return false;
                        seen.add(key);
                        return true;
                      })
                      .map((gap) => (
                        <rect
                          key={`gap:${gap.boot_id}:${gap.rom_id}:${gap.start_ms}`}
                          x={x(gap.start_ms)}
                          y="10"
                          width={Math.max(1, x(gap.end_ms) - x(gap.start_ms))}
                          height="130"
                          fill="currentColor"
                          opacity=".08"
                        >
                          <title>
                            Missing data:{' '}
                            {gapText(
                              gap.start_ms,
                              gap.end_ms,
                              result?.time_basis ?? 'uptime',
                              timezone,
                            )}
                          </title>
                        </rect>
                      ));
                  })()}
                  {chartSeries.map((item) => {
                    // The line runs through missing intervals; the shaded gaps show where data is
                    // missing. Elapsed-time charts start a new line at each boot, because a
                    // boot's times cannot be compared with another boot's.
                    const runs: string[][] = [];
                    item.points.forEach((p, i) => {
                      const previous = item.points[i - 1];
                      const joins =
                        i > 0 &&
                        (result?.time_basis === 'utc' ||
                          p.boot_id === previous.boot_id);
                      if (!joins) runs.push([]);
                      runs[runs.length - 1].push(
                        `${x(p.start_ms)},${y(p.average_c)}`,
                      );
                    });
                    return (
                      <g key={item.rom} color={item.colour}>
                        {runs.map((run, index) =>
                          run.length > 1 ? (
                            <polyline
                              key={index}
                              points={run.join(' ')}
                              fill="none"
                              stroke={item.colour}
                              strokeWidth="2"
                            />
                          ) : null,
                        )}
                        {item.points.map((p) => (
                          <circle
                            key={`${p.boot_id}:${p.start_ms}`}
                            cx={x(p.start_ms)}
                            cy={y(p.average_c)}
                            r="2.5"
                            fill={item.colour}
                          >
                            <title>
                              {item.name}:{' '}
                              {result?.time_basis === 'utc'
                                ? formatDeviceTime(p.start_ms, timezone)
                                : `${Math.floor(p.start_ms / 60000)} min since boot`}
                              : {temperature(p.average_c, unit)}; {p.count}{' '}
                              readings{p.partial ? '; partial' : ''}
                            </title>
                          </circle>
                        ))}
                      </g>
                    );
                  })}
                </svg>
                <Stack spacing={0.5} sx={{ mt: 1 }}>
                  {chartSeries.map((item) => (
                    <Typography key={item.rom}>
                      <span
                        aria-hidden="true"
                        style={{
                          display: 'inline-block',
                          width: 12,
                          height: 12,
                          marginRight: 6,
                          background: item.colour,
                        }}
                      />
                      {item.name}:{' '}
                      {temperature(
                        Math.min(...item.points.map((p) => p.minimum_c)),
                        unit,
                      )}{' '}
                      to{' '}
                      {temperature(
                        Math.max(...item.points.map((p) => p.maximum_c)),
                        unit,
                      )}{' '}
                      · {item.points.length} groups
                    </Typography>
                  ))}
                </Stack>
              </>
            )}
          </>
        )}
      </CardContent>
    </Card>
  );
}
