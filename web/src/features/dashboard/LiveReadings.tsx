// The live readings panel (spec §18.4): the Dashboard's one bold element. A deep evergreen panel
// in both themes, the room's state in words, and per probe a large light reading with a gauge of
// where it sits between its limits.
import type { ReactNode } from 'react';
import { Box, Typography } from '@mui/material';
import { useTheme } from '@mui/material/styles';
import ArrowDownwardRounded from '@mui/icons-material/ArrowDownwardRounded';
import ArrowUpwardRounded from '@mui/icons-material/ArrowUpwardRounded';
import LinkOffOutlined from '@mui/icons-material/LinkOffOutlined';
import { tokens } from '../../theme/tokens.generated';
import type { PanelProbe, RoomState } from './roomState';

// Out-of-range colours on the panel: the dark theme's status colours, which read on deep green.
const onPanel = tokens.dark.status;

interface Look {
  number: string;
  tag?: { text: string; icon: ReactNode; background: string; colour: string };
}

function look(probe: PanelProbe, text: string): Look {
  if (probe.state === 'high')
    return {
      number: onPanel.high.color,
      tag: {
        text: 'Too warm',
        icon: <ArrowUpwardRounded />,
        background: onPanel.high.container,
        colour: onPanel.high.onContainer,
      },
    };
  if (probe.state === 'low')
    return {
      number: onPanel.low.color,
      tag: {
        text: 'Too cold',
        icon: <ArrowDownwardRounded />,
        background: onPanel.low.container,
        colour: onPanel.low.onContainer,
      },
    };
  if (probe.state !== 'normal')
    return {
      number: text,
      tag: {
        text: probe.state === 'disconnected' ? 'Disconnected' : 'No reading',
        icon: <LinkOffOutlined />,
        background: tokens.dark.roles.errorContainer,
        colour: tokens.dark.roles.onErrorContainer,
      },
    };
  return { number: text };
}

const display = (celsius: number, unit: 'C' | 'F') =>
  unit === 'F' ? celsius * 1.8 + 32 : celsius;

// Where the reading and the limits fall on a scale reaching 3 °C beyond both, in percent.
function gauge(probe: PanelProbe) {
  const marks = [probe.celsius, probe.lowC, probe.highC].filter(
    (v): v is number => v !== null,
  );
  if (!marks.length) return null;
  const lo = Math.min(...marks) - 3;
  const hi = Math.max(...marks) + 3;
  const at = (v: number) =>
    Math.max(2, Math.min(98, ((v - lo) / (hi - lo)) * 100));
  const band =
    probe.lowC === null && probe.highC === null
      ? null
      : {
          left: probe.lowC === null ? 0 : at(probe.lowC),
          right: probe.highC === null ? 100 : at(probe.highC),
        };
  return { band, marker: probe.celsius === null ? null : at(probe.celsius) };
}

function rangeText(probe: PanelProbe, unit: 'C' | 'F') {
  const show = (v: number) => display(v, unit).toFixed(unit === 'F' ? 0 : 1);
  if (probe.lowC !== null && probe.highC !== null)
    return `Range ${show(probe.lowC)} to ${show(probe.highC)} °${unit}`;
  if (probe.highC !== null) return `Up to ${show(probe.highC)} °${unit}`;
  if (probe.lowC !== null) return `From ${show(probe.lowC)} °${unit}`;
  return 'No limits set';
}

function gaugeLabel(probe: PanelProbe, unit: 'C' | 'F') {
  const reading =
    probe.celsius === null
      ? 'no reading'
      : `${display(probe.celsius, unit).toFixed(1)} °${unit}`;
  const range = rangeText(probe, unit);
  return `${probe.name}: ${reading}, ${range.charAt(0).toLowerCase()}${range.slice(1)}`;
}

export default function LiveReadings({
  probes,
  state,
  unit,
  updated,
  live,
}: {
  probes: PanelProbe[];
  state: RoomState;
  unit: 'C' | 'F';
  updated: string;
  live: boolean;
}) {
  const theme = useTheme();
  const panel = theme.palette.panel;
  const hairline = `1px solid ${panel.text}29`; // tone 90 at 16 %
  return (
    <Box
      component="section"
      aria-label="Live readings"
      sx={{
        bgcolor: panel.background,
        color: panel.text,
        borderRadius: `${theme.corner.extraLarge}px`,
        py: { xs: 2.5, md: 4 },
        display: 'flex',
        flexDirection: 'column',
        gap: { xs: 1.5, md: 4.5 },
      }}
    >
      <Box
        sx={{
          px: { xs: 2.5, md: 4.5 },
          display: 'flex',
          flexWrap: 'wrap',
          alignItems: 'flex-start',
          justifyContent: 'space-between',
          gap: 2,
        }}
      >
        <Box sx={{ maxWidth: 680 }}>
          <Typography
            variant="headlineSmall"
            component="h2"
            aria-live="polite"
            sx={{
              color: panel.strong,
              fontSize: { md: '2.25rem' },
              lineHeight: { md: '2.75rem' },
            }}
          >
            {state.headline}
          </Typography>
          <Typography
            variant="bodyMedium"
            sx={{ mt: 1, color: panel.muted, fontSize: { md: '1rem' } }}
          >
            {state.detail}
          </Typography>
        </Box>
        <Box
          sx={{
            display: 'flex',
            alignItems: 'center',
            gap: 1.25,
            minHeight: 32,
            color: panel.muted,
            ...theme.typography.bodyMedium,
          }}
        >
          <span
            className={live ? 'live-dot' : 'live-dot live-dot-still'}
            aria-hidden="true"
          />
          {updated}
        </Box>
      </Box>
      <Box
        component="ul"
        aria-label="Probes"
        sx={{
          m: 0,
          px: { xs: 2.5, md: 1.5 },
          display: 'grid',
          gridTemplateColumns: {
            xs: 'minmax(0, 1fr)',
            md: `repeat(${Math.max(1, probes.length)}, minmax(0, 1fr))`,
          },
        }}
      >
        {probes.map((probe, i) => {
          const value =
            probe.celsius === null
              ? '—'
              : display(probe.celsius, unit).toFixed(1);
          const style = look(probe, panel.strong);
          const scale = gauge(probe);
          return (
            <Box
              component="li"
              key={probe.rom}
              sx={{
                listStyle: 'none',
                minWidth: 0,
                display: 'grid',
                gridTemplateAreas: {
                  xs: '"name value" "gauge value"',
                  md: '"name" "value" "gauge" "range"',
                },
                gridTemplateColumns: {
                  xs: 'minmax(0, 1fr) auto',
                  md: 'minmax(0, 1fr)',
                },
                alignItems: 'center',
                columnGap: 2,
                rowGap: { xs: 1, md: 1.75 },
                py: { xs: 1.25, md: 0.5 },
                px: { xs: 0, md: 3 },
                borderTop: { xs: hairline, md: 'none' },
                borderLeft: { xs: 'none', md: i ? hairline : 'none' },
              }}
            >
              <Box
                sx={{
                  gridArea: 'name',
                  display: 'flex',
                  alignItems: 'center',
                  justifyContent: { md: 'space-between' },
                  gap: 1,
                  minHeight: 24,
                  minWidth: 0,
                }}
              >
                <Typography
                  variant="titleSmall"
                  component="span"
                  noWrap
                  sx={{ color: panel.muted }}
                >
                  {probe.name}
                </Typography>
                {style.tag && (
                  <Box
                    component="span"
                    sx={{
                      display: 'flex',
                      alignItems: 'center',
                      gap: 0.5,
                      height: 24,
                      pl: 0.75,
                      pr: 1,
                      borderRadius: `${theme.corner.small}px`,
                      bgcolor: style.tag.background,
                      color: style.tag.colour,
                      whiteSpace: 'nowrap',
                      ...theme.typography.labelMedium,
                      '& svg': { fontSize: 14 },
                    }}
                  >
                    <Box
                      component="span"
                      aria-hidden="true"
                      sx={{ display: 'flex' }}
                    >
                      {style.tag.icon}
                    </Box>
                    {style.tag.text}
                  </Box>
                )}
              </Box>
              <Box
                sx={{
                  gridArea: 'value',
                  display: 'flex',
                  alignItems: 'baseline',
                  gap: 0.5,
                }}
              >
                <Typography
                  component="span"
                  sx={{
                    fontSize: { xs: '2.25rem', md: '4rem' },
                    lineHeight: { xs: '2.5rem', md: '4rem' },
                    fontWeight: 300,
                    letterSpacing: '-0.02em',
                    fontVariationSettings: "'opsz' 144",
                    color: style.number,
                  }}
                >
                  {value}
                </Typography>
                <Typography
                  component="span"
                  sx={{
                    fontSize: { xs: '0.875rem', md: '1.25rem' },
                    color: panel.muted,
                  }}
                >
                  {`°${unit}`}
                </Typography>
              </Box>
              <Box
                role="img"
                aria-label={gaugeLabel(probe, unit)}
                sx={{ gridArea: 'gauge', position: 'relative', height: 16 }}
              >
                <Box
                  sx={{
                    position: 'absolute',
                    inset: '7px 0 auto',
                    height: 2,
                    borderRadius: 1,
                    bgcolor: `${panel.text}2E`,
                  }}
                />
                {scale?.band && (
                  <Box
                    sx={{
                      position: 'absolute',
                      top: 6,
                      height: 4,
                      borderRadius: 2,
                      bgcolor: `${panel.text}80`,
                      left: `${scale.band.left}%`,
                      width: `${scale.band.right - scale.band.left}%`,
                    }}
                  />
                )}
                {scale?.marker !== null && scale?.marker !== undefined && (
                  <Box
                    sx={{
                      position: 'absolute',
                      top: 2,
                      width: 12,
                      height: 12,
                      ml: '-6px',
                      borderRadius: '50%',
                      left: `${scale.marker}%`,
                      bgcolor:
                        style.number === panel.strong
                          ? panel.text
                          : style.number,
                      boxShadow: `0 0 0 3px ${panel.background}`,
                    }}
                  />
                )}
              </Box>
              <Typography
                variant="bodySmall"
                sx={{
                  gridArea: 'range',
                  color: panel.muted,
                  display: { xs: 'none', md: 'block' },
                }}
              >
                {rangeText(probe, unit)}
              </Typography>
            </Box>
          );
        })}
      </Box>
    </Box>
  );
}
