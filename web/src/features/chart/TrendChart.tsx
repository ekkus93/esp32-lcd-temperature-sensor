// A temperature trend chart (spec §18.4, §18.5): smooth lines in the probes' series colours, light
// gridlines at round values, a time axis, and dashed lines at limits worth showing. Lines break
// where the history has a gap rather than drawing across it. Scales are in the display unit.
import { Box, Typography } from '@mui/material';
import { useTheme } from '@mui/material/styles';
import { niceScale, type TimeTick } from './scale';

export interface TrendPoint {
  start: number; // ms, in the history's time basis
  end: number;
  celsius: number;
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

const WIDTH = 960;

const display = (celsius: number, unit: 'C' | 'F') =>
  unit === 'F' ? celsius * 1.8 + 32 : celsius;

// A smooth path through the points (Catmull-Rom as cubic Béziers), one subpath per unbroken run.
function path(points: { x: number; y: number }[][]): string {
  return points
    .filter((run) => run.length > 0)
    .map((run) => {
      if (run.length === 1) return `M${run[0].x},${run[0].y}h0.01`;
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
  height = 240,
  label,
}: {
  series: TrendSeries[];
  from: number;
  to: number;
  ticks: TimeTick[];
  unit: 'C' | 'F';
  limits?: TrendLimit[];
  height?: number;
  label: string;
}) {
  const theme = useTheme();
  const values = [
    ...series.flatMap((s) => s.points.map((p) => display(p.celsius, unit))),
    ...limits.map((l) => display(l.celsius, unit)),
  ];
  const scale = niceScale(values);
  const span = Math.max(1, to - from);
  const x = (t: number) => ((t - from) / span) * WIDTH;
  const y = (value: number) =>
    height - ((value - scale.min) / (scale.max - scale.min)) * height;
  const gridValues: number[] = [];
  for (let v = scale.min; v <= scale.max + 1e-9; v += scale.step)
    gridValues.push(Number(v.toFixed(2)));
  const decimals = scale.step < 1 ? 1 : 0;

  const lines = series.map((s) => {
    const sorted = [...s.points]
      .filter((p) => p.end > from && p.start < to)
      .sort((a, b) => a.start - b.start);
    const runs: { x: number; y: number }[][] = [[]];
    sorted.forEach((p, i) => {
      const previous = sorted[i - 1];
      const bucket = p.end - p.start;
      if (previous && p.start - previous.end > bucket * 1.5) runs.push([]);
      runs.at(-1)!.push({
        x: x((p.start + p.end) / 2),
        y: y(display(p.celsius, unit)),
      });
    });
    return { key: s.key, colour: s.colour, d: path(runs) };
  });

  const percentY = (value: number) => (y(value) / height) * 100;
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
        <Box sx={{ position: 'relative', height }}>
          <svg
            role="img"
            aria-label={label}
            width="100%"
            height={height}
            viewBox={`0 0 ${WIDTH} ${height}`}
            preserveAspectRatio="none"
            style={{ display: 'block', overflow: 'visible' }}
          >
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
              }}
            >
              {l.label}
            </Typography>
          ))}
        </Box>
        <Box
          aria-hidden="true"
          sx={{ position: 'relative', height: 20, mt: 1 }}
        >
          {ticks.map((tick) => (
            <Typography
              key={tick.at}
              variant="bodySmall"
              color="text.secondary"
              sx={{
                position: 'absolute',
                left: `${((tick.at - from) / span) * 100}%`,
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
