// A temperature trend chart (spec §18.4, §18.5): smooth lines in the probes' series colours, light
// gridlines at round values, a time axis, shaded missing intervals, dashed lines at limits worth
// showing, and each probe's value at the pointer on hover or tap. A line runs through a missing
// interval (the shading shows it) but not from one run to the next: without a trusted clock each
// boot is its own run, because one boot's times cannot be compared with another's.
import { useState, type PointerEvent } from 'react';
import { Box, Typography } from '@mui/material';
import { useTheme } from '@mui/material/styles';
import { niceScale, type TimeTick } from './scale';

export interface TrendPoint {
  start: number; // ms, in the history's time basis
  end: number;
  celsius: number;
  run?: string; // points join into one line only within the same run
}
export interface TrendSeries {
  key: string;
  name: string;
  colour: string;
  points: TrendPoint[];
}
export interface TrendLimit {
  celsius: number;
  colour: string; // the dashed line
  background: string; // the label's container and text colours
  text: string;
  label: string;
}
export interface TrendGap {
  start: number;
  end: number;
  label: string; // read out as "Missing data: <label>"
}

const WIDTH = 960;

const display = (celsius: number, unit: 'C' | 'F') =>
  unit === 'F' ? celsius * 1.8 + 32 : celsius;

// A smooth path through the points (Catmull-Rom as cubic Béziers), one subpath per run.
function path(runs: { x: number; y: number }[][]): string {
  return runs
    .filter((run) => run.length > 0)
    .map((run) => {
      if (run.length === 1)
        return `M${run[0].x.toFixed(1)},${run[0].y.toFixed(1)}h0.01`;
      let d = `M${run[0].x.toFixed(1)},${run[0].y.toFixed(1)}`;
      for (let i = 0; i < run.length - 1; i++) {
        const p0 = run[Math.max(0, i - 1)];
        const p1 = run[i];
        const p2 = run[i + 1];
        const p3 = run[Math.min(run.length - 1, i + 2)];
        const c1x = p1.x + (p2.x - p0.x) / 6;
        const c1y = p1.y + (p2.y - p0.y) / 6;
        const c2x = p2.x - (p3.x - p1.x) / 6;
        const c2y = p2.y - (p3.y - p1.y) / 6;
        d += ` C${c1x.toFixed(1)},${c1y.toFixed(1)} ${c2x.toFixed(1)},${c2y.toFixed(1)} ${p2.x.toFixed(1)},${p2.y.toFixed(1)}`;
      }
      return d;
    })
    .join(' ');
}

export default function TrendChart({
  series,
  from,
  to,
  ticks,
  unit,
  limits = [],
  gaps = [],
  height = 240,
  label,
  formatTime,
}: {
  series: TrendSeries[];
  from: number;
  to: number;
  ticks: TimeTick[];
  unit: 'C' | 'F';
  limits?: TrendLimit[];
  gaps?: TrendGap[];
  height?: number;
  label: string;
  formatTime: (at: number) => string;
}) {
  const theme = useTheme();
  const [pointer, setPointer] = useState<number | null>(null); // fraction across the chart
  const visible = series.map((s) => ({
    ...s,
    points: [...s.points]
      .filter((p) => p.end > from && p.start < to)
      .sort((a, b) => a.start - b.start),
  }));
  const values = [
    ...visible.flatMap((s) => s.points.map((p) => display(p.celsius, unit))),
    ...limits.map((l) => display(l.celsius, unit)),
  ];
  const scale = niceScale(values);
  const span = Math.max(1, to - from);
  const x = (t: number) => ((t - from) / span) * WIDTH;
  const y = (value: number) =>
    height - ((value - scale.min) / (scale.max - scale.min)) * height;
  const percentX = (t: number) => ((t - from) / span) * 100;
  const percentY = (value: number) => (y(value) / height) * 100;
  const gridValues: number[] = [];
  for (let v = scale.min; v <= scale.max + 1e-9; v += scale.step)
    gridValues.push(Number(v.toFixed(2)));
  const decimals = scale.step < 1 ? 1 : 0;

  const lines = visible.map((s) => {
    const runs: { x: number; y: number }[][] = [];
    s.points.forEach((p, i) => {
      if (i === 0 || p.run !== s.points[i - 1].run) runs.push([]);
      runs.at(-1)!.push({
        x: x((p.start + p.end) / 2),
        y: y(display(p.celsius, unit)),
      });
    });
    return { key: s.key, colour: s.colour, d: path(runs), runs: runs.length };
  });

  // The value of each probe nearest the pointer, within about one five-minute bucket.
  const at = pointer === null ? null : from + pointer * span;
  const readout =
    at === null
      ? []
      : visible.flatMap((s) => {
          let best: TrendPoint | null = null;
          for (const p of s.points) {
            const middle = (p.start + p.end) / 2;
            if (
              Math.abs(middle - at) <= (p.end - p.start) * 1.5 &&
              (!best ||
                Math.abs(middle - at) <
                  Math.abs((best.start + best.end) / 2 - at))
            )
              best = p;
          }
          return best ? [{ series: s, point: best }] : [];
        });
  const track = (event: PointerEvent<HTMLDivElement>) => {
    const box = event.currentTarget.getBoundingClientRect();
    if (box.width <= 0) return;
    setPointer(
      Math.min(1, Math.max(0, (event.clientX - box.left) / box.width)),
    );
  };

  return (
    <Box sx={{ display: 'flex', gap: 1.5 }}>
      <Box
        aria-hidden="true"
        sx={{ position: 'relative', width: 40, height, flex: '0 0 40px' }}
      >
        {gridValues.map((v) => (
          <Typography
            key={v}
            variant="bodySmall"
            color="text.secondary"
            sx={{
              position: 'absolute',
              right: 0,
              top: `${percentY(v)}%`,
              transform: 'translateY(-50%)',
              whiteSpace: 'nowrap',
            }}
          >
            {`${v.toFixed(decimals)}°`}
          </Typography>
        ))}
      </Box>
      <Box sx={{ flex: '1 1 auto', minWidth: 0 }}>
        <Box
          sx={{ position: 'relative', height, touchAction: 'pan-y' }}
          onPointerMove={track}
          onPointerDown={track}
          onPointerLeave={() => setPointer(null)}
        >
          <svg
            role="img"
            aria-label={label}
            width="100%"
            height={height}
            viewBox={`0 0 ${WIDTH} ${height}`}
            preserveAspectRatio="none"
            style={{ display: 'block', overflow: 'visible' }}
          >
            {gaps.map((gap) => (
              <rect
                key={`${gap.start}:${gap.end}`}
                x={x(Math.max(from, gap.start))}
                y={0}
                width={Math.max(
                  1,
                  x(Math.min(to, gap.end)) - x(Math.max(from, gap.start)),
                )}
                height={height}
                fill={theme.palette.m3.onSurface}
                fillOpacity={0.07}
              >
                <title>{`Missing data: ${gap.label}`}</title>
              </rect>
            ))}
            {gridValues.map((v) => (
              <line
                key={v}
                x1={0}
                x2={WIDTH}
                y1={y(v)}
                y2={y(v)}
                stroke={theme.palette.m3.outlineVariant}
                strokeOpacity={0.6}
                strokeWidth={1}
                vectorEffect="non-scaling-stroke"
              />
            ))}
            {limits.map((l) => (
              <line
                key={l.label}
                x1={0}
                x2={WIDTH}
                y1={y(display(l.celsius, unit))}
                y2={y(display(l.celsius, unit))}
                stroke={l.colour}
                strokeWidth={1.5}
                strokeDasharray="6 6"
                vectorEffect="non-scaling-stroke"
              />
            ))}
            {lines.map((line) => (
              <path
                key={line.key}
                d={line.d}
                fill="none"
                stroke={line.colour}
                strokeWidth={2.25}
                strokeLinejoin="round"
                strokeLinecap="round"
                vectorEffect="non-scaling-stroke"
                data-series={line.key}
                data-runs={line.runs}
              />
            ))}
          </svg>
          {limits.map((l) => (
            <Typography
              key={l.label}
              variant="labelMedium"
              sx={{
                position: 'absolute',
                right: 0,
                top: `${percentY(display(l.celsius, unit))}%`,
                transform: 'translateY(-125%)',
                px: 1,
                py: 0.25,
                borderRadius: '6px',
                bgcolor: l.background,
                color: l.text,
                pointerEvents: 'none',
              }}
            >
              {l.label}
            </Typography>
          ))}
          {at !== null && readout.length > 0 && (
            <Box aria-hidden="true" sx={{ pointerEvents: 'none' }}>
              <Box
                sx={{
                  position: 'absolute',
                  top: 0,
                  bottom: 0,
                  left: `${pointer! * 100}%`,
                  width: '1px',
                  bgcolor: theme.palette.m3.outline,
                }}
              />
              {readout.map(({ series: s, point }) => (
                <Box
                  key={s.key}
                  sx={{
                    position: 'absolute',
                    width: 10,
                    height: 10,
                    ml: '-5px',
                    mt: '-5px',
                    borderRadius: '50%',
                    left: `${percentX((point.start + point.end) / 2)}%`,
                    top: `${percentY(display(point.celsius, unit))}%`,
                    bgcolor: s.colour,
                    boxShadow: `0 0 0 2px ${theme.palette.background.paper}`,
                  }}
                />
              ))}
              <Box
                sx={{
                  position: 'absolute',
                  top: 8,
                  ...(pointer! > 0.6
                    ? { right: `calc(${(1 - pointer!) * 100}% + 12px)` }
                    : { left: `calc(${pointer! * 100}% + 12px)` }),
                  px: 1.5,
                  py: 1,
                  borderRadius: `${theme.corner.small}px`,
                  bgcolor: theme.palette.m3.inverseSurface,
                  color: theme.palette.m3.inverseOnSurface,
                  boxShadow: 2,
                  minWidth: 140,
                }}
              >
                <Typography variant="labelMedium" component="p">
                  {formatTime(at)}
                </Typography>
                {readout.map(({ series: s, point }) => (
                  <Box
                    key={s.key}
                    sx={{
                      display: 'flex',
                      alignItems: 'center',
                      gap: 1,
                      mt: 0.5,
                    }}
                  >
                    <Box
                      sx={{
                        width: 8,
                        height: 8,
                        borderRadius: '50%',
                        bgcolor: s.colour,
                        flex: '0 0 8px',
                      }}
                    />
                    <Typography variant="bodySmall" sx={{ flex: '1 1 auto' }}>
                      {s.name}
                    </Typography>
                    <Typography variant="labelMedium">
                      {`${display(point.celsius, unit).toFixed(1)} °${unit}`}
                    </Typography>
                  </Box>
                ))}
              </Box>
            </Box>
          )}
        </Box>
        <Box
          aria-hidden="true"
          sx={{ position: 'relative', height: 20, mt: 1 }}
        >
          {ticks.map((tick, i) => (
            <Typography
              key={tick.at}
              variant="bodySmall"
              color="text.secondary"
              sx={{
                // On a phone every other label, counted back from the last, so they don't collide.
                display: {
                  xs: (ticks.length - 1 - i) % 2 ? 'none' : 'block',
                  sm: 'block',
                },
                position: 'absolute',
                left: `${percentX(tick.at)}%`,
                // Keep the end labels inside the chart.
                transform:
                  tick.at >= to
                    ? 'translateX(-100%)'
                    : tick.at <= from
                      ? 'none'
                      : 'translateX(-50%)',
                whiteSpace: 'nowrap',
              }}
            >
              {tick.label}
            </Typography>
          ))}
        </Box>
      </Box>
    </Box>
  );
}
