// The app's light theme around a component under test, as it is inside the app: views take their
// status and chart colours from the theme. Use renderThemed (renderThemed.ts) in tests.
import type { ReactNode } from 'react';
import { ThemeProvider } from '@mui/material/styles';
import { createAppTheme } from './theme/theme';

const lightTheme = createAppTheme('light');
export function AppTheme({ children }: { children: ReactNode }) {
  return <ThemeProvider theme={lightTheme}>{children}</ThemeProvider>;
}
