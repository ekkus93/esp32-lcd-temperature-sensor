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
