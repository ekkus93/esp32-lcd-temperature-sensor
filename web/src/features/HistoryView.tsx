import { useEffect, useMemo, useState, type ReactNode } from 'react';
import {
  Alert,
  Box,
  Card,
  CardContent,
  Chip,
  IconButton,
  Menu,
  MenuItem,
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableRow,
  ToggleButton,
  ToggleButtonGroup,
  Tooltip,
  Typography,
} from '@mui/material';
import { useTheme } from '@mui/material/styles';
import ChevronLeftRounded from '@mui/icons-material/ChevronLeftRounded';
import ChevronRightRounded from '@mui/icons-material/ChevronRightRounded';
import DownloadOutlined from '@mui/icons-material/DownloadOutlined';
import InfoOutlined from '@mui/icons-material/InfoOutlined';
import MoreVertOutlined from '@mui/icons-material/MoreVertOutlined';
import RefreshOutlined from '@mui/icons-material/RefreshOutlined';
import ZoomInOutlined from '@mui/icons-material/ZoomInOutlined';
import ZoomOutMapOutlined from '@mui/icons-material/ZoomOutMapOutlined';
import ZoomOutOutlined from '@mui/icons-material/ZoomOutOutlined';
import {
  getHistory,
  historyExportUrl,
  type HistoryResult,
} from '../api/history';
import { temperature } from '../api/client';
import type { Snapshot } from '../api/generated';
import TrendChart from './chart/TrendChart';
import { timeTicks } from './chart/scale';
import { chartGaps, chartSeries, pointTime } from './chart/historySeries';

const ranges = [
  { label: '1 hour', hours: 1 },
  { label: '24 hours', hours: 24 },
  { label: '7 days', hours: 168 },
  { label: '30 days', hours: 720 },
];

// An icon button whose name is its tooltip.
function Action({
  label,
  onClick,
  children,
}: {
  label: string;
  onClick: () => void;
  children: ReactNode;
}) {
  return (
    <Tooltip title={label}>
      <IconButton onClick={onClick}>{children}</IconButton>
    </Tooltip>
  );
}

export default function HistoryView({
  snapshot,
  unit,
  timezone,
}: {
  snapshot: Snapshot;
  unit: 'C' | 'F';
  timezone: string | null;
}) {
  // Probes are coloured by display position in the hues of the LCD's Trends lines, set for the
  // current light or dark theme (spec §18.2), so a probe looks the same everywhere.
  const probeColours = useTheme().palette.series;
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
  const [menu, setMenu] = useState<HTMLElement | null>(null);
  const theme = useTheme();
  // The selected probes, in position order, with their names and colours.
  const probes = series.map((rom) => {
    const sensor = snapshot.sensors.find((item) => item.rom_id === rom);
    return {
      rom,
      name: sensor?.name ?? `Probe ${rom.slice(-4).toUpperCase()}`,
      colour: probeColours[(sensor?.order ?? 0) % probeColours.length],
    };
  });
  const shown = probes.filter((probe) => selectedRoms.includes(probe.rom));
  const lines = result
    ? chartSeries(result, shown).filter((line) =>
        line.points.some((p) => p.end > viewFrom && p.start < viewTo),
      )
    : [];
  const basis = result?.time_basis ?? 'uptime';
  // Low, average and high of each line in the window, weighted by the readings in each bucket.
  const summary = lines.map((line) => {
    const points = (result?.points ?? []).filter(
      (p) =>
        p.rom_id === line.key && p.end_ms > viewFrom && p.start_ms < viewTo,
    );
    const readings = points.reduce((sum, p) => sum + p.count, 0);
    return {
      line,
      low: Math.min(...points.map((p) => p.minimum_c)),
      high: Math.max(...points.map((p) => p.maximum_c)),
      average:
        points.reduce((sum, p) => sum + p.average_c * p.count, 0) /
        Math.max(1, readings),
      readings,
    };
  });
  const canZoomIn = viewport.scale < 8 && Boolean(result?.points.length);
  const zoomed = viewport.scale > 1;
  const canGoEarlier = zoomed && viewport.offset < 1 - 1 / viewport.scale;
  const canGoLater = zoomed && viewport.offset > 0;
  const notice = [
    result?.simulated ? 'Simulated history.' : '',
    basis === 'uptime'
      ? 'Clock unsynchronized: showing elapsed time for this boot only.'
      : timezone
        ? `Times shown in ${timezone}.`
        : 'Device timezone unavailable; synchronized times shown in UTC.',
    'Missing intervals are shaded.',
  ]
    .filter(Boolean)
    .join(' ');

  return (
    <Card>
      <CardContent sx={{ display: 'flex', flexDirection: 'column', gap: 2.5 }}>
        <Box
          sx={{
            display: 'flex',
            flexWrap: 'wrap',
            alignItems: 'center',
            justifyContent: 'space-between',
            gap: 1.5,
          }}
        >
          <Typography variant="titleLarge" component="h2">
            Five-minute temperature history
          </Typography>
          <Box
            sx={{
              display: 'flex',
              flexWrap: 'wrap',
              alignItems: 'center',
              gap: 0.5,
            }}
          >
            <ToggleButtonGroup
              exclusive
              value={hours}
              onChange={(_, next: number | null) => next && setHours(next)}
              aria-label="Time range"
              sx={{
                mr: 1,
                width: { xs: '100%', sm: 'auto' },
                '& .MuiToggleButton-root': {
                  flex: { xs: '1 1 0', sm: 'none' },
                  px: { xs: 1, sm: 2 },
                },
              }}
            >
              {ranges.map((range) => (
                <ToggleButton key={range.hours} value={range.hours}>
                  {range.label}
                </ToggleButton>
              ))}
            </ToggleButtonGroup>
            {zoomed && (
              <Action
                label="Zoom out"
                onClick={() => setViewport(viewport.scale / 2, viewport.offset)}
              >
                <ZoomOutOutlined />
              </Action>
            )}
            {canZoomIn && (
              <Action
                label="Zoom in"
                onClick={() => {
                  const nextScale = Math.min(8, viewport.scale * 2);
                  const center = viewFrom + windowSpan / 2;
                  setViewport(
                    nextScale,
                    (dataTo - center - dataSpan / nextScale / 2) / dataSpan,
                  );
                }}
              >
                <ZoomInOutlined />
              </Action>
            )}
            {canGoEarlier && (
              <Action
                label="Earlier"
                onClick={() =>
                  setViewport(
                    viewport.scale,
                    viewport.offset + 0.5 / viewport.scale,
                  )
                }
              >
                <ChevronLeftRounded />
              </Action>
            )}
            {canGoLater && (
              <Action
                label="Later"
                onClick={() =>
                  setViewport(
                    viewport.scale,
                    viewport.offset - 0.5 / viewport.scale,
                  )
                }
              >
                <ChevronRightRounded />
              </Action>
            )}
            {zoomed && (
              <Action label="Reset zoom" onClick={() => setViewport(1, 0)}>
                <ZoomOutMapOutlined />
              </Action>
            )}
            <Action
              label="Refresh history"
              onClick={() => setRefresh(refresh + 1)}
            >
              <RefreshOutlined />
            </Action>
            <Tooltip title="More">
              <IconButton
                aria-haspopup="menu"
                aria-expanded={Boolean(menu)}
                onClick={(event) => setMenu(event.currentTarget)}
              >
                <MoreVertOutlined />
              </IconButton>
            </Tooltip>
            <Menu
              anchorEl={menu}
              open={Boolean(menu)}
              onClose={() => setMenu(null)}
            >
              <MenuItem
                component="a"
                href={exportUrl}
                target="_blank"
                rel="noopener"
                disabled={!exportUrl}
                onClick={() => setMenu(null)}
              >
                <DownloadOutlined fontSize="small" sx={{ mr: 1.5 }} />
                Export five-minute CSV
              </MenuItem>
            </Menu>
          </Box>
        </Box>
        {error && <Alert severity="error">{error}</Alert>}
        {!error && !result && <Typography>Loading history…</Typography>}
        {result && (
          <>
            {result.truncated && (
              <Alert severity="warning">
                Some overlapping or mixed-boot records were omitted. Narrow the
                range.
              </Alert>
            )}
            <Box
              sx={{
                display: 'flex',
                alignItems: 'flex-start',
                gap: 1,
                color: 'text.secondary',
              }}
            >
              <InfoOutlined
                aria-hidden="true"
                sx={{ fontSize: 18, mt: '1px' }}
              />
              <Typography variant="bodyMedium">{notice}</Typography>
            </Box>
            {!result.points.length && (
              <Typography>No history available in this range.</Typography>
            )}
            {probes.length > 0 && (
              <Box
                role="group"
                aria-label="Probes shown"
                sx={{ display: 'flex', flexWrap: 'wrap', gap: 1 }}
              >
                {probes.map((probe) => {
                  const on = selectedRoms.includes(probe.rom);
                  return (
                    <Tooltip
                      key={probe.rom}
                      title={`ROM ${probe.rom}`}
                      describeChild
                    >
                      <Chip
                        clickable
                        variant={on ? 'filled' : 'outlined'}
                        aria-pressed={on}
                        label={probe.name}
                        onClick={() =>
                          setSeriesSelection({
                            key,
                            roms: on
                              ? selectedRoms.filter((rom) => rom !== probe.rom)
                              : [...selectedRoms, probe.rom],
                          })
                        }
                        icon={
                          <Box
                            component="span"
                            aria-hidden="true"
                            sx={{
                              width: 10,
                              height: 10,
                              borderRadius: '50%',
                              bgcolor: probe.colour,
                              opacity: on ? 1 : 0.5,
                              ml: '10px !important',
                            }}
                          />
                        }
                      />
                    </Tooltip>
                  );
                })}
              </Box>
            )}
            {result.points.length > 0 && zoomed && !lines.length && (
              <Typography>
                No data in this zoomed view. Zoom out, or use Earlier and Later.
              </Typography>
            )}
            {lines.length > 0 && (
              <>
                <TrendChart
                  series={lines}
                  from={viewFrom}
                  to={viewTo}
                  ticks={timeTicks(viewFrom, viewTo, basis, timezone)}
                  unit={unit}
                  gaps={chartGaps(
                    result,
                    lines.map((line) => line.key),
                    timezone,
                  )}
                  height={280}
                  formatTime={(at) => pointTime(at, basis, timezone)}
                  label={`Five-minute average temperature chart for ${lines.length} ${lines.length === 1 ? 'probe' : 'probes'}`}
                />
                <Box sx={{ overflowX: 'auto' }}>
                  <Table
                    size="small"
                    aria-label="Range summary"
                    sx={{
                      '& th, & td': {
                        whiteSpace: 'nowrap',
                        px: { xs: 1, sm: 2 },
                      },
                      '& .readings': {
                        display: { xs: 'none', sm: 'table-cell' },
                      },
                    }}
                  >
                    <TableHead>
                      <TableRow>
                        <TableCell>Probe</TableCell>
                        <TableCell align="right">Low</TableCell>
                        <TableCell align="right">Average</TableCell>
                        <TableCell align="right">High</TableCell>
                        <TableCell align="right" className="readings">
                          Readings
                        </TableCell>
                      </TableRow>
                    </TableHead>
                    <TableBody>
                      {summary.map((row) => (
                        <TableRow key={row.line.key}>
                          <TableCell>
                            <Box
                              sx={{
                                display: 'flex',
                                alignItems: 'center',
                                gap: 1,
                              }}
                            >
                              <Box
                                aria-hidden="true"
                                sx={{
                                  width: 10,
                                  height: 10,
                                  borderRadius: '50%',
                                  bgcolor: row.line.colour,
                                  flex: '0 0 10px',
                                }}
                              />
                              {row.line.name}
                            </Box>
                          </TableCell>
                          <TableCell align="right">
                            {temperature(row.low, unit)}
                          </TableCell>
                          <TableCell align="right">
                            {temperature(row.average, unit)}
                          </TableCell>
                          <TableCell align="right">
                            {temperature(row.high, unit)}
                          </TableCell>
                          <TableCell
                            align="right"
                            className="readings"
                            sx={{ color: theme.palette.text.secondary }}
                          >
                            {row.readings.toLocaleString()}
                          </TableCell>
                        </TableRow>
                      ))}
                    </TableBody>
                  </Table>
                </Box>
              </>
            )}
          </>
        )}
      </CardContent>
    </Card>
  );
}
