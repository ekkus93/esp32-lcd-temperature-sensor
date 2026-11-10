import { expect, it } from 'vitest';
import { requestTimeoutMs } from './auth';

it('gives the slow password checks longer than the device takes to answer', () => {
  // A first login with a password saved by older firmware (100,000 rounds) took about 31 seconds
  // on the board before it is rehashed; the request must outlast it.
  expect(requestTimeoutMs('/auth/login')).toBeGreaterThan(31000);
  expect(requestTimeoutMs('/auth/enroll')).toBeGreaterThan(31000);
  expect(requestTimeoutMs('/auth/password')).toBeGreaterThan(31000);
  // Ordinary requests keep the short limit.
  expect(requestTimeoutMs('/settings')).toBe(30000);
});
