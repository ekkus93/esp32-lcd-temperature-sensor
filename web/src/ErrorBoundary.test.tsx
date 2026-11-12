import { render, screen } from '@testing-library/react';
import { expect, it, vi } from 'vitest';
import ErrorBoundary from './ErrorBoundary';

function Broken(): never {
  throw new Error('chunk failed');
}

it('shows a reload message instead of a blank page when a page fails', () => {
  vi.spyOn(console, 'error').mockImplementation(() => {});
  render(
    <ErrorBoundary>
      <Broken />
    </ErrorBoundary>,
  );
  expect(screen.getByRole('alert')).toHaveTextContent(
    'This page could not be shown',
  );
  expect(screen.getByRole('button', { name: 'Reload' })).toBeInTheDocument();
  vi.mocked(console.error).mockRestore();
});
