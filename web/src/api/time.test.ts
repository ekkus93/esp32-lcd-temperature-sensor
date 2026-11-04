import { expect, it } from 'vitest';
import { formatDeviceTime } from './time';

it('formats synchronized timestamps in the selected device timezone', () => {
  expect(
    formatDeviceTime(Date.UTC(2026, 0, 1, 12), 'America/Los_Angeles'),
  ).toBe('Jan 1, 2026, 4:00 AM PST (America/Los_Angeles)');
});

it('uses explicitly labeled UTC when the selected timezone is unavailable', () => {
  expect(formatDeviceTime(Date.UTC(2026, 0, 1, 12), null)).toBe(
    'Jan 1, 2026, 12:00 PM UTC',
  );
});

it('formats timestamps across a daylight saving transition in the device zone', () => {
  expect(
    formatDeviceTime(Date.UTC(2026, 2, 8, 10, 30), 'America/Los_Angeles'),
  ).toBe('Mar 8, 2026, 3:30 AM PDT (America/Los_Angeles)');
});

it('distinguishes the repeated hour at the daylight saving end transition', () => {
  expect(
    formatDeviceTime(Date.UTC(2026, 10, 1, 8, 30), 'America/Los_Angeles'),
  ).toBe('Nov 1, 2026, 1:30 AM PDT (America/Los_Angeles)');
  expect(
    formatDeviceTime(Date.UTC(2026, 10, 1, 9, 30), 'America/Los_Angeles'),
  ).toBe('Nov 1, 2026, 1:30 AM PST (America/Los_Angeles)');
});
