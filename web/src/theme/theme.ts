// The app's Material 3 theme (spec §18.2) on top of MUI. Colours come from the generated tokens;
// the type scale, shapes, motion and component styles follow the Material 3 guidelines.
import {
  createTheme,
  type CSSProperties,
  type Theme,
} from '@mui/material/styles';
import robotoFlex from '../assets/fonts/roboto-flex-latin.woff2';
import { tokens } from './tokens.generated';
import type { Panel, Role, Scheme, Status, StatusColor } from './tokenSource';

export type Mode = 'light' | 'dark';
type TypeRole =
  | 'displayLarge'
  | 'displayMedium'
  | 'displaySmall'
  | 'headlineLarge'
  | 'headlineMedium'
  | 'headlineSmall'
  | 'titleLarge'
  | 'titleMedium'
  | 'titleSmall'
  | 'bodyLarge'
  | 'bodyMedium'
  | 'bodySmall'
  | 'labelLarge'
  | 'labelMedium'
  | 'labelSmall';

declare module '@mui/material/styles' {
  interface Palette {
    panel: Panel;
    m3: Record<Role, string>;
    status: Record<Status, StatusColor>;
    series: string[];
  }
  interface PaletteOptions {
    panel?: Panel;
    m3?: Record<Role, string>;
    status?: Record<Status, StatusColor>;
    series?: string[];
  }
  // eslint-disable-next-line @typescript-eslint/no-empty-object-type -- augmentation: merges members into MUI's interface
  interface TypographyVariants extends Record<TypeRole, CSSProperties> {}
  // eslint-disable-next-line @typescript-eslint/no-empty-object-type -- augmentation: merges members into MUI's interface
  interface TypographyVariantsOptions extends Partial<
    Record<TypeRole, CSSProperties>
  > {}
  interface Theme {
    corner: typeof corner;
    motion: typeof motion;
  }
  interface ThemeOptions {
    corner?: typeof corner;
    motion?: typeof motion;
  }
}
declare module '@mui/material/Typography' {
  // eslint-disable-next-line @typescript-eslint/no-empty-object-type -- augmentation: merges members into MUI's interface
  interface TypographyPropsVariantOverrides extends Record<TypeRole, true> {}
}
declare module '@mui/material/Button' {
  interface ButtonPropsVariantOverrides {
    tonal: true;
  }
}

export const fontFamily =
  '"Roboto Flex", system-ui, -apple-system, "Segoe UI", sans-serif';

// Material 3 type scale: size / line height in px, weight, tracking in px.
const scale: Record<TypeRole, [number, number, number, number]> = {
  displayLarge: [57, 64, 400, -0.25],
  displayMedium: [45, 52, 400, 0],
  displaySmall: [36, 44, 400, 0],
  headlineLarge: [32, 40, 400, 0],
  headlineMedium: [28, 36, 400, 0],
  headlineSmall: [24, 32, 400, 0],
  titleLarge: [22, 28, 400, 0],
  titleMedium: [16, 24, 500, 0.15],
  titleSmall: [14, 20, 500, 0.1],
  bodyLarge: [16, 24, 400, 0.5],
  bodyMedium: [14, 20, 400, 0.25],
  bodySmall: [12, 16, 400, 0.4],
  labelLarge: [14, 20, 500, 0.1],
  labelMedium: [12, 16, 500, 0.5],
  labelSmall: [11, 16, 500, 0.5],
};
export const type = Object.fromEntries(
  Object.entries(scale).map(([role, [size, line, weight, tracking]]) => [
    role,
    {
      fontFamily,
      fontSize: `${size / 16}rem`,
      lineHeight: `${line / 16}rem`,
      fontWeight: weight,
      letterSpacing: tracking ? `${tracking / size}em` : 0,
    } satisfies CSSProperties,
  ]),
) as Record<TypeRole, CSSProperties>;

// Material 3 shape scale, in px.
export const corner = {
  none: 0,
  extraSmall: 4,
  small: 8,
  medium: 12,
  large: 16,
  extraLarge: 28,
  full: 9999,
} as const;

// Material 3 easing and durations (ms).
export const motion = {
  easing: {
    standard: 'cubic-bezier(0.2, 0, 0, 1)',
    standardDecelerate: 'cubic-bezier(0, 0, 0, 1)',
    standardAccelerate: 'cubic-bezier(0.3, 0, 1, 1)',
    emphasizedDecelerate: 'cubic-bezier(0.05, 0.7, 0.1, 1)',
    emphasizedAccelerate: 'cubic-bezier(0.3, 0, 0.8, 0.15)',
  },
  duration: {
    short1: 50,
    short2: 100,
    short3: 150,
    short4: 200,
    medium1: 250,
    medium2: 300,
    medium3: 350,
    medium4: 400,
    long1: 450,
    long2: 500,
  },
} as const;

// Material 3 state layers: the content colour laid over the container at these opacities.
export const stateLayer = {
  hover: 0.08,
  focus: 0.1,
  pressed: 0.1,
  dragged: 0.16,
} as const;

// `colour` with `opacity` as #RRGGBBAA.
export function withAlpha(colour: string, opacity: number): string {
  return (
    colour +
    Math.round(opacity * 255)
      .toString(16)
      .padStart(2, '0')
      .toUpperCase()
  );
}
// `over` laid on `base` at `opacity`, as an opaque colour (for hover states on filled surfaces).
export function overlay(base: string, over: string, opacity: number): string {
  const channel = (hex: string, i: number) =>
    parseInt(hex.slice(1 + 2 * i, 3 + 2 * i), 16);
  return (
    '#' +
    [0, 1, 2]
      .map((i) =>
        Math.round(
          channel(base, i) * (1 - opacity) + channel(over, i) * opacity,
        )
          .toString(16)
          .padStart(2, '0'),
      )
      .join('')
      .toUpperCase()
  );
}

const kebab = (name: string) =>
  name.replace(/[A-Z]/g, (c) => `-${c.toLowerCase()}`);

// CSS custom properties named as in the Material 3 design kit (--md-sys-color-surface, ...), for
// the few styles kept in styles.css.
export function cssVariables(scheme: Scheme): string {
  const lines = [
    ...Object.entries(scheme.roles).map(
      ([role, value]) => `--md-sys-color-${kebab(role)}: ${value};`,
    ),
    ...Object.entries(scheme.status).flatMap(([status, colours]) =>
      Object.entries(colours).map(
        ([part, value]) => `--app-status-${status}-${kebab(part)}: ${value};`,
      ),
    ),
    ...scheme.series.map((value, i) => `--app-series-${i + 1}: ${value};`),
    ...Object.entries(tokens.panel).map(
      ([part, value]) => `--app-panel-${kebab(part)}: ${value};`,
    ),
  ];
  return `:root { ${lines.join(' ')} }`;
}

export function createAppTheme(mode: Mode): Theme {
  const scheme = tokens[mode];
  const c = scheme.roles;
  const warning = scheme.status.warning;
  const elevation1 =
    mode === 'light'
      ? '0 1px 2px 0 rgba(0,0,0,0.3), 0 1px 3px 1px rgba(0,0,0,0.15)'
      : '0 1px 3px 1px rgba(0,0,0,0.15), 0 1px 2px 0 rgba(0,0,0,0.3)';
  const elevation2 =
    mode === 'light'
      ? '0 1px 2px 0 rgba(0,0,0,0.3), 0 2px 6px 2px rgba(0,0,0,0.15)'
      : '0 2px 6px 2px rgba(0,0,0,0.15), 0 1px 2px 0 rgba(0,0,0,0.3)';
  const containerAlert = (background: string, text: string, icon: string) => ({
    backgroundColor: background,
    color: text,
    '& .MuiAlert-icon': { color: icon },
  });

  return createTheme({
    palette: {
      mode,
      primary: {
        main: c.primary,
        contrastText: c.onPrimary,
        dark: c.onPrimaryContainer,
        light: c.primaryContainer,
      },
      secondary: { main: c.secondary, contrastText: c.onSecondary },
      error: { main: c.error, contrastText: c.onError },
      warning: { main: warning.color, contrastText: warning.onColor },
      info: { main: c.tertiary, contrastText: c.onTertiary },
      success: { main: c.primary, contrastText: c.onPrimary },
      background: { default: c.surface, paper: c.surfaceContainerLow },
      text: {
        primary: c.onSurface,
        secondary: c.onSurfaceVariant,
        disabled: withAlpha(c.onSurface, 0.38),
      },
      divider: c.outlineVariant,
      action: {
        active: c.onSurfaceVariant,
        hover: withAlpha(c.onSurface, stateLayer.hover),
        hoverOpacity: stateLayer.hover,
        selected: withAlpha(c.onSurface, stateLayer.pressed),
        focus: withAlpha(c.onSurface, stateLayer.focus),
        focusOpacity: stateLayer.focus,
        disabled: withAlpha(c.onSurface, 0.38),
        disabledBackground: withAlpha(c.onSurface, 0.12),
      },
      m3: c,
      panel: tokens.panel,
      status: scheme.status,
      series: scheme.series,
    },
    typography: {
      fontFamily,
      htmlFontSize: 16,
      ...type,
      h1: type.displayLarge,
      h2: type.displayMedium,
      h3: type.displaySmall,
      h4: type.headlineLarge,
      h5: type.headlineSmall,
      h6: type.titleLarge,
      subtitle1: type.titleMedium,
      subtitle2: type.titleSmall,
      body1: type.bodyLarge,
      body2: type.bodyMedium,
      caption: type.bodySmall,
      button: { ...type.labelLarge, textTransform: 'none' },
      overline: { ...type.labelMedium, textTransform: 'none' },
    },
    shape: { borderRadius: corner.medium },
    corner,
    motion,
    transitions: {
      easing: {
        easeInOut: motion.easing.standard,
        easeOut: motion.easing.standardDecelerate,
        easeIn: motion.easing.standardAccelerate,
        sharp: motion.easing.standard,
      },
      duration: {
        shortest: motion.duration.short2,
        shorter: motion.duration.short3,
        short: motion.duration.short4,
        standard: motion.duration.medium2,
        complex: motion.duration.medium4,
        enteringScreen: motion.duration.medium4,
        leavingScreen: motion.duration.short4,
      },
    },
    components: {
      MuiCssBaseline: {
        styleOverrides: `
          @font-face {
            font-family: "Roboto Flex";
            src: url(${robotoFlex}) format("woff2-variations"), url(${robotoFlex}) format("woff2");
            font-weight: 300 700;
            font-style: normal;
            font-display: swap;
          }
          ${cssVariables(scheme)}
          body { background: ${c.surface}; color: ${c.onSurface}; }
          :focus-visible { outline: 2px solid ${c.secondary}; outline-offset: 2px; }
        `,
      },
      MuiTypography: {
        defaultProps: {
          variantMapping: {
            displayLarge: 'h1',
            displayMedium: 'h2',
            displaySmall: 'h3',
            headlineLarge: 'h1',
            headlineMedium: 'h2',
            headlineSmall: 'h3',
            titleLarge: 'h2',
            titleMedium: 'h3',
            titleSmall: 'h4',
            bodyLarge: 'p',
            bodyMedium: 'p',
            bodySmall: 'p',
            labelLarge: 'span',
            labelMedium: 'span',
            labelSmall: 'span',
          },
        },
      },
      MuiPaper: {
        styleOverrides: { root: { backgroundImage: 'none' } },
      },
      MuiCard: {
        defaultProps: { elevation: 0 },
        styleOverrides: {
          root: {
            borderRadius: corner.large,
            backgroundColor: c.surfaceContainerLow,
            border: `1px solid ${c.outlineVariant}`,
            boxShadow: 'none',
          },
        },
      },
      MuiButton: {
        defaultProps: { disableElevation: true },
        styleOverrides: {
          root: {
            ...type.labelLarge,
            borderRadius: corner.full,
            minHeight: 40,
            padding: '10px 24px',
            textTransform: 'none',
          },
          sizeSmall: { minHeight: 32, padding: '6px 16px' },
          text: { padding: '10px 12px' },
          outlined: { borderColor: c.outline, color: c.primary },
        },
        variants: [
          {
            props: { variant: 'tonal' },
            style: {
              backgroundColor: c.secondaryContainer,
              color: c.onSecondaryContainer,
              '&:hover': {
                backgroundColor: overlay(
                  c.secondaryContainer,
                  c.onSecondaryContainer,
                  stateLayer.hover,
                ),
                boxShadow: elevation1,
              },
              '&.Mui-disabled': {
                backgroundColor: withAlpha(c.onSurface, 0.12),
                color: withAlpha(c.onSurface, 0.38),
              },
            },
          },
          {
            props: { variant: 'contained' },
            style: { '&:hover': { boxShadow: elevation1 } },
          },
        ],
      },
      MuiIconButton: {
        styleOverrides: { root: { color: c.onSurfaceVariant } },
      },
      MuiToggleButton: {
        styleOverrides: {
          root: {
            ...type.labelLarge,
            textTransform: 'none',
            whiteSpace: 'nowrap',
            minHeight: 40,
            padding: '0 16px',
            borderRadius: corner.full,
            borderColor: c.outline,
            color: c.onSurface,
            '&.Mui-selected, &.Mui-selected:hover': {
              backgroundColor: c.secondaryContainer,
              color: c.onSecondaryContainer,
            },
          },
          sizeSmall: { minHeight: 32, padding: '0 12px' },
        },
      },
      MuiTabs: {
        styleOverrides: {
          root: { borderBottom: `1px solid ${c.surfaceVariant}` },
          indicator: {
            height: 3,
            borderRadius: '3px 3px 0 0',
            backgroundColor: c.primary,
          },
        },
      },
      MuiTab: {
        styleOverrides: {
          root: {
            ...type.titleSmall,
            textTransform: 'none',
            minHeight: 48,
            color: c.onSurfaceVariant,
            '&.Mui-selected': { color: c.primary },
          },
        },
      },
      MuiSwitch: {
        styleOverrides: {
          root: {
            width: 52,
            height: 32,
            padding: 0,
            margin: 8,
            overflow: 'visible',
          },
          switchBase: {
            padding: 0,
            width: 32,
            height: 32,
            color: c.outline,
            '&.Mui-checked': {
              transform: 'translateX(20px)',
              color: c.onPrimary,
              '& + .MuiSwitch-track': {
                backgroundColor: c.primary,
                borderColor: c.primary,
                opacity: 1,
              },
              '& .MuiSwitch-thumb': { width: 24, height: 24 },
            },
            '&.Mui-disabled + .MuiSwitch-track': { opacity: 0.12 },
            '&.Mui-disabled .MuiSwitch-thumb': { opacity: 0.38 },
            // Keyboard focus lands on a transparent input, so the global focus ring cannot show;
            // ring the track instead, as every other control is ringed.
            '&.Mui-focusVisible + .MuiSwitch-track': {
              outline: `2px solid ${c.secondary}`,
              outlineOffset: 2,
            },
          },
          thumb: {
            width: 16,
            height: 16,
            boxShadow: 'none',
            transition: `width ${motion.duration.short4}ms, height ${motion.duration.short4}ms`,
          },
          track: {
            borderRadius: 16,
            border: `2px solid ${c.outline}`,
            backgroundColor: c.surfaceContainerHighest,
            opacity: 1,
            boxSizing: 'border-box',
          },
        },
      },
      MuiCheckbox: {
        styleOverrides: { root: { color: c.onSurfaceVariant } },
      },
      MuiChip: {
        styleOverrides: {
          root: { ...type.labelLarge, borderRadius: corner.small, height: 32 },
          outlined: { borderColor: c.outlineVariant },
        },
        variants: [
          {
            props: { variant: 'filled', color: 'default' },
            style: {
              backgroundColor: c.secondaryContainer,
              color: c.onSecondaryContainer,
            },
          },
        ],
      },
      MuiAlert: {
        styleOverrides: {
          root: {
            ...type.bodyMedium,
            borderRadius: corner.medium,
            alignItems: 'center',
          },
        },
        variants: [
          {
            props: { variant: 'standard', severity: 'error' },
            style: containerAlert(
              c.errorContainer,
              c.onErrorContainer,
              c.error,
            ),
          },
          {
            props: { variant: 'standard', severity: 'warning' },
            style: containerAlert(
              warning.container,
              warning.onContainer,
              warning.color,
            ),
          },
          {
            props: { variant: 'standard', severity: 'info' },
            style: containerAlert(
              c.tertiaryContainer,
              c.onTertiaryContainer,
              c.tertiary,
            ),
          },
          {
            props: { variant: 'standard', severity: 'success' },
            style: containerAlert(
              c.primaryContainer,
              c.onPrimaryContainer,
              c.primary,
            ),
          },
        ],
      },
      MuiOutlinedInput: {
        styleOverrides: {
          root: {
            borderRadius: corner.extraSmall,
            '& .MuiOutlinedInput-notchedOutline': { borderColor: c.outline },
            '&:hover:not(.Mui-focused):not(.Mui-error) .MuiOutlinedInput-notchedOutline':
              {
                borderColor: c.onSurface,
              },
          },
        },
      },
      MuiInputLabel: {
        styleOverrides: { root: { color: c.onSurfaceVariant } },
      },
      MuiFormHelperText: {
        styleOverrides: {
          root: { ...type.bodySmall, color: c.onSurfaceVariant },
        },
      },
      MuiDialog: {
        styleOverrides: {
          paper: {
            borderRadius: corner.extraLarge,
            backgroundColor: c.surfaceContainerHigh,
          },
        },
      },
      MuiDialogTitle: {
        styleOverrides: { root: { ...type.headlineSmall, color: c.onSurface } },
      },
      MuiPopover: {
        styleOverrides: {
          paper: {
            borderRadius: corner.extraSmall,
            backgroundColor: c.surfaceContainer,
            boxShadow: elevation2,
          },
        },
      },
      MuiTooltip: {
        styleOverrides: {
          tooltip: {
            ...type.bodySmall,
            backgroundColor: c.inverseSurface,
            color: c.inverseOnSurface,
            borderRadius: corner.extraSmall,
          },
        },
      },
      MuiSnackbarContent: {
        styleOverrides: {
          root: {
            ...type.bodyMedium,
            backgroundColor: c.inverseSurface,
            color: c.inverseOnSurface,
            borderRadius: corner.extraSmall,
            boxShadow: elevation2,
          },
          action: { color: c.inversePrimary },
        },
      },
      MuiLinearProgress: {
        styleOverrides: {
          root: { borderRadius: 2, backgroundColor: c.surfaceContainerHighest },
          bar: { borderRadius: 2, backgroundColor: c.primary },
        },
      },
      MuiBadge: {
        styleOverrides: { badge: { ...type.labelSmall } },
      },
      MuiDivider: {
        styleOverrides: { root: { borderColor: c.outlineVariant } },
      },
    },
  });
}
