import { fireEvent, screen } from '@testing-library/react';
import { expect, it } from 'vitest';
import TrendChart, { type TrendSeries } from './TrendChart';
import { renderThemed as render } from '../../renderThemed';

const bucket = 300_000;
const series = (run: (i: number) => string): TrendSeries[] => [
  {
    key: 'canopy',
    name: 'Canopy',
    colour: '#006FAE',
    points: [0, 1, 2, 3].map((i) => ({
      start: i * bucket,
      end: (i + 1) * bucket,
      celsius: 24 + i,
      run: run(i),
    })),
  },
];

function chart(lines: TrendSeries[]) {
  return render(
    <TrendChart
      series={lines}
      from={0}
      to={4 * bucket}
      ticks={[]}
      unit="C"
      label="Test chart"
      formatTime={(at) => `${at / 60000} min since boot`}
    />,
  );
}

it('starts a new line for each run, such as each boot without a trusted clock', () => {
  const { container } = chart(series((i) => (i < 2 ? 'boot-a' : 'boot-b')));
  expect(
    container.querySelector('path[data-series]')?.getAttribute('data-runs'),
  ).toBe('2');
});

it('shows each probe’s value at the pointer', () => {
  chart(series(() => 'one'));
  const area = screen.getByRole('img', { name: 'Test chart' }).parentElement!;
  area.getBoundingClientRect = () =>
    ({ left: 0, width: 400, top: 0, height: 240 }) as DOMRect;
  // Three eighths of the way: the middle of the second bucket (25 °C).
  fireEvent.pointerMove(area, { clientX: 150 });
  expect(screen.getByText('7.5 min since boot')).toBeInTheDocument();
  expect(screen.getByText('25.0 °C')).toBeInTheDocument();
  fireEvent.pointerLeave(area);
  expect(screen.queryByText('25.0 °C')).toBeNull();
});

it('never draws a line backwards in time or past the readings when points are unevenly spaced', () => {
  // Five-minute buckets beside multi-day holes, as on a 30-day chart after reboots.
  const day = 86_400_000;
  const at = [
    0,
    bucket,
    5 * day,
    5 * day + bucket,
    5 * day + 2 * bucket,
    9 * day,
  ];
  const celsius = [19, 21, 18, 23, 17, 20];
  const { container } = render(
    <TrendChart
      series={[
        {
          key: 'canopy',
          name: 'Canopy',
          colour: '#006FAE',
          points: at.map((start, i) => ({
            start,
            end: start + bucket,
            celsius: celsius[i],
            run: 'utc',
          })),
        },
      ]}
      from={0}
      to={9 * day + bucket}
      ticks={[]}
      unit="C"
      label="Test chart"
      formatTime={(t) => `${t}`}
    />,
  );
  const d = container.querySelector('path[data-series]')!.getAttribute('d')!;
  // Every coordinate pair in the path, control points included, in drawing order.
  const pairs = [...d.matchAll(/(-?[\d.]+),(-?[\d.]+)/g)].map((m) => ({
    x: Number(m[1]),
    y: Number(m[2]),
  }));
  expect(pairs.length).toBeGreaterThan(at.length);
  for (let i = 1; i < pairs.length; i++)
    expect(pairs[i].x).toBeGreaterThanOrEqual(pairs[i - 1].x);
  // Control points stay between their segment's two readings, so the curve cannot overshoot.
  const ends = pairs.filter((_, i) => i === 0 || i % 3 === 0);
  const ys = ends.map((p) => p.y);
  for (const p of pairs) {
    expect(p.y).toBeGreaterThanOrEqual(Math.min(...ys) - 0.05);
    expect(p.y).toBeLessThanOrEqual(Math.max(...ys) + 0.05);
  }
});

it('crosses a long outage with a dashed bridge, not the curve', () => {
  const day = 86_400_000;
  const { container } = render(
    <TrendChart
      series={[
        {
          key: 'canopy',
          name: 'Canopy',
          colour: '#006FAE',
          points: [0, bucket, 2 * day, 2 * day + bucket].map((start, i) => ({
            start,
            end: start + bucket,
            celsius: 20 + i,
            run: 'utc',
            afterGap: i === 2,
          })),
        },
      ]}
      from={0}
      to={2 * day + 2 * bucket}
      ticks={[]}
      unit="C"
      label="Test chart"
      formatTime={(t) => `${t}`}
    />,
  );
  // Two solid stretches, and one dashed straight segment between them.
  expect(
    container.querySelector('path[data-series]')?.getAttribute('data-runs'),
  ).toBe('2');
  const bridge = container.querySelector('path[data-bridges]')!;
  expect(bridge.getAttribute('stroke-dasharray')).toBe('2 6');
  expect(bridge.getAttribute('d')).toMatch(/^M[\d.]+,[\d.]+ L[\d.]+,[\d.]+$/);
});

it('draws no bridge between runs, such as boots without a trusted clock', () => {
  const { container } = chart(series((i) => (i < 2 ? 'boot-a' : 'boot-b')));
  expect(container.querySelector('path[data-bridges]')).toBeNull();
});
