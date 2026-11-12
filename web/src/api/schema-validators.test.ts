import { readFileSync } from 'node:fs';
import { expect, it } from 'vitest';
import { authStateValid } from './schema-validators.generated';

it('generates plain ES modules, so the dev server can load them', () => {
  const source = readFileSync('src/api/schema-validators.generated.ts', 'utf8');
  expect(source).not.toMatch(/\brequire\(/);
});

it('counts string length in characters, a surrogate pair once, as Ajv does', () => {
  const state = (csrf: string) => ({ enrolled: true, signed_in: true, csrf });
  expect(authStateValid(state('a'.repeat(32)))).toBe(true);
  expect(authStateValid(state('a'.repeat(31)))).toBe(false);
  expect(authStateValid(state('a'.repeat(65)))).toBe(false);
  // 32 emoji are 64 UTF-16 units but 32 characters: still within 32..64.
  expect(authStateValid(state('😀'.repeat(32)))).toBe(true);
  // 33 emoji are 66 UTF-16 units but 33 characters: also within the limit.
  expect(authStateValid(state('😀'.repeat(33)))).toBe(true);
  // 65 emoji are 65 characters: too long.
  expect(authStateValid(state('😀'.repeat(65)))).toBe(false);
});
