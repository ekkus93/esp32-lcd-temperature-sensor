// How an active alert looks wherever it is listed (the Alerts tab, and the Dashboard): an icon in
// its status colour, what is wrong in words, and since when (spec §18.6).
import type { ReactNode } from 'react';
import { Box, Typography } from '@mui/material';
import { useTheme, type Theme } from '@mui/material/styles';
import ArrowDownwardRounded from '@mui/icons-material/ArrowDownwardRounded';
import ArrowUpwardRounded from '@mui/icons-material/ArrowUpwardRounded';
import LinkOffOutlined from '@mui/icons-material/LinkOffOutlined';
import ErrorOutlineOutlined from '@mui/icons-material/ErrorOutlineOutlined';
import WifiOffOutlined from '@mui/icons-material/WifiOffOutlined';
import {
  alertDetail,
  alertSince,
  alertTitle,
  type ActiveAlert,
} from './alertText';
import type { StatusColor } from '../theme/tokenSource';

function tone(item: ActiveAlert, theme: Theme): StatusColor {
  const { status, m3 } = theme.palette;
  if (item.state === 'high') return status.high;
  if (item.state === 'low') return status.low;
  if (item.state === 'wifi_disconnected') return status.warning;
  return {
    color: m3.error,
    onColor: m3.onError,
    container: m3.errorContainer,
    onContainer: m3.onErrorContainer,
  };
}

function icon(item: ActiveAlert): ReactNode {
  if (item.state === 'high') return <ArrowUpwardRounded fontSize="small" />;
  if (item.state === 'low') return <ArrowDownwardRounded fontSize="small" />;
  if (item.state === 'wifi_disconnected')
    return <WifiOffOutlined fontSize="small" />;
  if (item.state === 'bus_fault')
    return <ErrorOutlineOutlined fontSize="small" />;
  return <LinkOffOutlined fontSize="small" />;
}

export function AlertItem({
  item,
  timezone,
}: {
  item: ActiveAlert;
  timezone: string | null;
}) {
  const theme = useTheme();
  const colours = tone(item, theme);
  const extra = alertDetail(item);
  return (
    <Box
      component="li"
      sx={{
        listStyle: 'none',
        display: 'flex',
        alignItems: 'center',
        gap: 1.75,
        p: 2,
        borderRadius: `${theme.corner.large}px`,
        bgcolor: colours.container,
        color: colours.onContainer,
      }}
    >
      <Box
        aria-hidden="true"
        sx={{
          flex: '0 0 40px',
          height: 40,
          borderRadius: '20px',
          bgcolor: colours.color,
          color: colours.onColor,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
        }}
      >
        {icon(item)}
      </Box>
      <Box sx={{ minWidth: 0 }}>
        <Typography variant="titleMedium" component="p">
          {alertTitle(item)}
        </Typography>
        {extra && (
          <Typography variant="bodyMedium" component="p">
            {extra}
          </Typography>
        )}
        <Typography variant="bodyMedium" component="p">
          {alertSince(item, timezone)}
        </Typography>
      </Box>
    </Box>
  );
}

export function AlertList({
  items,
  timezone,
}: {
  items: ActiveAlert[];
  timezone: string | null;
}) {
  return (
    <Box
      component="ul"
      aria-label="Active alerts"
      sx={{ m: 0, p: 0, display: 'flex', flexDirection: 'column', gap: 1 }}
    >
      {items.map((item, i) => (
        <AlertItem
          key={'rom_id' in item ? item.rom_id : `${item.state}-${i}`}
          item={item}
          timezone={timezone}
        />
      ))}
    </Box>
  );
}
