import type { ReactElement } from 'react';
import { render } from '@testing-library/react';
import { AppTheme } from './testTheme';

// Renders a view inside the app's light theme.
export function renderThemed(ui: ReactElement) {
  return render(ui, { wrapper: AppTheme });
}
