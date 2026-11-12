import { describe, expect, it } from 'vitest';
import { statSync } from 'node:fs';
import { resolve } from 'node:path';
import { buildTokens, seed } from './tokenSource';
import { tokens } from './tokens.generated';
import { createAppTheme, cssVariables, overlay, withAlpha } from './theme';
import type { Scheme } from './tokenSource';

// WCAG 2.1 relative luminance and contrast ratio.
function luminance(hex: string): number {
  const [r, g, b] = [1, 3, 5].map((i) => {
    const c = parseInt(hex.slice(i, i + 2), 16) / 255;
    return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
  });
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}
function contrast(a: string, b: string): number {
  const [high, low] = [luminance(a), luminance(b)].sort((x, y) => y - x);
  return (high + 0.05) / (low + 0.05);
}

// Pairs the app draws text with (4.5:1, WCAG AA for normal text).
function textPairs(s: Scheme): [string, string, string][] {
  const r = s.roles;
  const surfaces = [
    'surface',
    'surfaceContainerLow',
    'surfaceContainer',
    'surfaceContainerHigh',
  ] as const;
  return [
    ['onPrimary on primary', r.onPrimary, r.primary],
    [
      'onPrimaryContainer on primaryContainer',
      r.onPrimaryContainer,
      r.primaryContainer,
    ],
    [
      'onSecondaryContainer on secondaryContainer',
      r.onSecondaryContainer,
      r.secondaryContainer,
    ],
    [
      'onTertiaryContainer on tertiaryContainer',
      r.onTertiaryContainer,
      r.tertiaryContainer,
    ],
    ['onError on error', r.onError, r.error],
    [
      'onErrorContainer on errorContainer',
      r.onErrorContainer,
      r.errorContainer,
    ],
    [
      'inverseOnSurface on inverseSurface',
      r.inverseOnSurface,
      r.inverseSurface,
    ],
    ...surfaces.flatMap((surface): [string, string, string][] => [
      [`onSurface on ${surface}`, r.onSurface, r[surface]],
      [`onSurfaceVariant on ${surface}`, r.onSurfaceVariant, r[surface]],
      [`primary on ${surface}`, r.primary, r[surface]],
      [`error on ${surface}`, r.error, r[surface]],
      ...Object.entries(s.status).map(([name, c]): [string, string, string] => [
        `${name} on ${surface}`,
        c.color,
        r[surface],
      ]),
    ]),
    ...Object.entries(s.status).flatMap(
      ([name, c]): [string, string, string][] => [
        [`${name} onColor on color`, c.onColor, c.color],
        [`${name} onContainer on container`, c.onContainer, c.container],
      ],
    ),
  ];
}
// Pairs for component boundaries and chart lines (3:1, WCAG AA for non-text contrast).
function graphicPairs(s: Scheme): [string, string, string][] {
  const r = s.roles;
  return [
    ['outline on surface', r.outline, r.surface],
    ['outline on surfaceContainerLow', r.outline, r.surfaceContainerLow],
    ...s.series.flatMap((colour, i): [string, string, string][] => [
      [`series ${i + 1} on surface`, colour, r.surface],
      [`series ${i + 1} on surfaceContainerLow`, colour, r.surfaceContainerLow],
    ]),
  ];
}

describe('Material 3 colour tokens', () => {
  it('match what the generator makes from the seed (run npm run theme:generate)', () => {
    expect(tokens).toEqual(buildTokens());
  });

  it('use the seed and key roles recorded in the spec', () => {
    expect(seed).toBe('#2F6B4F');
    expect(tokens.light.roles.primary).toBe('#236A4C');
    expect(tokens.light.roles.surface).toBe('#F5FBF4');
    expect(tokens.dark.roles.primary).toBe('#8FD5B0');
    expect(tokens.dark.roles.surface).toBe('#0F1511');
    expect(tokens.light.status.high.color).toBe('#9B4600');
    expect(tokens.light.status.low.color).toBe('#006491');
    expect(tokens.light.status.warning.color).toBe('#775A00');
  });

  it('keep the LCD Trends hue order for the five chart series', () => {
    // Blue, yellow, red, purple, green, as firmware display_view.cpp `line_colors`.
    const hue = (hex: string) => {
      const [r, g, b] = [1, 3, 5].map(
        (i) => parseInt(hex.slice(i, i + 2), 16) / 255,
      );
      const max = Math.max(r, g, b);
      const d = max - Math.min(r, g, b);
      const h =
        max === r
          ? ((g - b) / d) % 6
          : max === g
            ? (b - r) / d + 2
            : (r - g) / d + 4;
      return (h * 60 + 360) % 360;
    };
    for (const mode of ['light', 'dark'] as const) {
      const hues = tokens[mode].series.map(hue);
      expect(hues[0]).toBeGreaterThan(190); // blue
      expect(hues[0]).toBeLessThan(230);
      expect(hues[1]).toBeGreaterThan(30); // yellow-orange
      expect(hues[1]).toBeLessThan(50);
      expect(hues[2]).toBeGreaterThan(10); // red-orange
      expect(hues[2]).toBeLessThan(30);
      expect(hues[3]).toBeGreaterThan(300); // purple-pink
      expect(hues[4]).toBeGreaterThan(140); // green
      expect(hues[4]).toBeLessThan(170);
    }
  });

  for (const mode of ['light', 'dark'] as const) {
    it(`meet WCAG AA contrast for text in the ${mode} theme`, () => {
      const failing = textPairs(tokens[mode])
        .map(([name, fg, bg]) => [name, contrast(fg, bg)] as const)
        .filter(([, ratio]) => ratio < 4.5);
      expect(failing).toEqual([]);
    });
    it(`meet WCAG AA contrast for boundaries and chart lines in the ${mode} theme`, () => {
      const failing = graphicPairs(tokens[mode])
        .map(([name, fg, bg]) => [name, contrast(fg, bg)] as const)
        .filter(([, ratio]) => ratio < 3);
      expect(failing).toEqual([]);
    });
  }
});

describe('the live readings panel', () => {
  it('is the seed palette at tone 20 with readable text, in both themes', () => {
    expect(tokens.panel.background).toBe('#003824');
    const { background, strong, text, muted } = tokens.panel;
    for (const colour of [strong, text, muted])
      expect(contrast(colour, background)).toBeGreaterThanOrEqual(4.5);
    // Out-of-range readings on the panel use the dark theme's status colours.
    for (const status of Object.values(tokens.dark.status))
      expect(contrast(status.color, background)).toBeGreaterThanOrEqual(4.5);
  });
});

describe('the app theme', () => {
  it('takes its palette from the tokens for each mode', () => {
    for (const mode of ['light', 'dark'] as const) {
      const theme = createAppTheme(mode);
      expect(theme.palette.mode).toBe(mode);
      expect(theme.palette.primary.main).toBe(tokens[mode].roles.primary);
      expect(theme.palette.background.default).toBe(tokens[mode].roles.surface);
      expect(theme.palette.text.primary).toBe(tokens[mode].roles.onSurface);
      expect(theme.palette.warning.main).toBe(
        tokens[mode].status.warning.color,
      );
      expect(theme.palette.series).toEqual(tokens[mode].series);
      expect(theme.palette.m3.outlineVariant).toBe(
        tokens[mode].roles.outlineVariant,
      );
    }
  });

  it('uses Roboto Flex and the Material 3 type scale', () => {
    const theme = createAppTheme('light');
    expect(theme.typography.fontFamily).toMatch(/^"Roboto Flex"/);
    expect(theme.typography.displayMedium.fontSize).toBe('2.8125rem'); // 45 px
    expect(theme.typography.bodyLarge.lineHeight).toBe('1.5rem'); // 24 px
    expect(theme.typography.labelLarge.fontWeight).toBe(500);
    expect(theme.typography.h6).toEqual(theme.typography.titleLarge);
    expect(theme.typography.button.textTransform).toBe('none');
  });

  it('exposes the roles as Material 3 CSS variables', () => {
    const css = cssVariables(tokens.dark);
    expect(css).toContain(
      `--md-sys-color-outline-variant: ${tokens.dark.roles.outlineVariant};`,
    );
    expect(css).toContain(
      `--app-status-high-container: ${tokens.dark.status.high.container};`,
    );
    expect(css).toContain(`--app-series-5: ${tokens.dark.series[4]};`);
    expect(css).toContain(
      `--app-panel-background: ${tokens.panel.background};`,
    );
  });

  it('mixes state-layer colours', () => {
    expect(withAlpha('#236A4C', 0.08)).toBe('#236A4C14');
    expect(overlay('#FFFFFF', '#000000', 0.5)).toBe('#808080');
  });
});

describe('the built-in font', () => {
  it('stays within the 80 KB limit (spec §18.2)', () => {
    // Tests run from web/.
    const path = resolve('src/assets/fonts/roboto-flex-latin.woff2');
    expect(statSync(path).size).toBeLessThanOrEqual(80 * 1024);
  });
});
