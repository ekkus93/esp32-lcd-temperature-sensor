// Builds the Material 3 colour tokens from one seed colour with Google's
// material-color-utilities. Only `scripts/generate-theme.mjs` and the token test import this file,
// so the colour library is a development dependency and never reaches the device: the app reads
// the generated `tokens.generated.ts` instead.
import {
  argbFromHex,
  Blend,
  DynamicColor,
  hexFromArgb,
  Hct,
  MaterialDynamicColors,
  SchemeTonalSpot,
  TonalPalette,
} from '@material/material-color-utilities';

// A calm evergreen that suits a grow room (spec §18.2, decision D10).
export const seed = '#2F6B4F';

// The Material 3 colour roles the app uses, by their names in the colour library.
export const roles = [
  'primary',
  'onPrimary',
  'primaryContainer',
  'onPrimaryContainer',
  'inversePrimary',
  'secondary',
  'onSecondary',
  'secondaryContainer',
  'onSecondaryContainer',
  'tertiary',
  'onTertiary',
  'tertiaryContainer',
  'onTertiaryContainer',
  'error',
  'onError',
  'errorContainer',
  'onErrorContainer',
  'background',
  'onBackground',
  'surface',
  'surfaceDim',
  'surfaceBright',
  'surfaceContainerLowest',
  'surfaceContainerLow',
  'surfaceContainer',
  'surfaceContainerHigh',
  'surfaceContainerHighest',
  'onSurface',
  'surfaceVariant',
  'onSurfaceVariant',
  'inverseSurface',
  'inverseOnSurface',
  'outline',
  'outlineVariant',
  'shadow',
  'scrim',
  'surfaceTint',
] as const;
export type Role = (typeof roles)[number];

// Status colours are Material 3 custom colours: a source colour harmonised toward the seed, then
// the usual four tones of a colour role. Faults use the error roles; normal uses primary-container.
export const statusSources = {
  high: '#D84315', // too warm
  low: '#1E88E5', // too cold
  warning: '#F9A825', // stale, clock not set, Wi-Fi lost, simulation
} as const;
export type Status = keyof typeof statusSources;
export interface StatusColor {
  color: string;
  onColor: string;
  container: string;
  onContainer: string;
}

// Chart series in probe display order. The hues follow the LCD's Trends lines (blue, yellow, red,
// purple, green; firmware display_view.cpp `line_colors`) so a probe looks the same on both, taken
// from the colour-blind-safe Okabe-Ito set and set to tone 45 (light) or 75 (dark).
export const seriesSources = [
  '#0072B2',
  '#E69F00',
  '#D55E00',
  '#CC79A7',
  '#009E73',
] as const;

export interface Scheme {
  roles: Record<Role, string>;
  status: Record<Status, StatusColor>;
  series: string[];
}
export interface Tokens {
  seed: string;
  light: Scheme;
  dark: Scheme;
}

const hex = (argb: number) => hexFromArgb(argb).toUpperCase();

function scheme(dark: boolean): Scheme {
  const source = argbFromHex(seed);
  const dynamic = new SchemeTonalSpot(Hct.fromInt(source), dark, 0);
  const colours = MaterialDynamicColors as unknown as Record<
    Role,
    DynamicColor
  >;
  const rolesOut = Object.fromEntries(
    roles.map((role) => [role, hex(colours[role].getArgb(dynamic))]),
  ) as Record<Role, string>;
  const status = Object.fromEntries(
    Object.entries(statusSources).map(([name, colour]) => {
      const palette = TonalPalette.fromInt(
        Blend.harmonize(argbFromHex(colour), source),
      );
      const tone = (light: number, darkTone: number) =>
        hex(palette.tone(dark ? darkTone : light));
      return [
        name,
        {
          color: tone(40, 80),
          onColor: tone(100, 20),
          container: tone(90, 30),
          onContainer: tone(10, 90),
        },
      ];
    }),
  ) as Record<Status, StatusColor>;
  const series = seriesSources.map((colour) => {
    const hct = Hct.fromInt(argbFromHex(colour));
    return hex(
      Hct.from(hct.hue, Math.max(hct.chroma, 48), dark ? 75 : 45).toInt(),
    );
  });
  return { roles: rolesOut, status, series };
}

export function buildTokens(): Tokens {
  return { seed, light: scheme(false), dark: scheme(true) };
}
